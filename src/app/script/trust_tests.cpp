// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "tests/app_test.h"

#include "app/script/trust.h"

#include "lauxlib.h"
#include "lualib.h"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

using namespace app::script;

namespace {

int probe(lua_State* L)
{
  lua_pushboolean(L, is_trusted_call(L));
  return 1;
}

// A stand-in of the app's Process class: start() notes the program
// and whether the call is trusted, and starts nothing
struct Start {
  std::string program;
  bool trusted;
};
std::vector<Start> g_starts;

int fake_process_start(lua_State* L)
{
  luaL_checkudata(L, 1, "FakeProcess");
  lua_getiuservalue(L, 1, 1);
  g_starts.push_back({ luaL_checkstring(L, -1), is_trusted_call(L) });
  return 0;
}

int fake_process_new(lua_State* L)
{
  luaL_checktype(L, 1, LUA_TTABLE);
  lua_newuserdatauv(L, 0, 1);
  lua_rawgeti(L, 1, 1);
  lua_setiuservalue(L, -2, 1);
  luaL_setmetatable(L, "FakeProcess");
  return 1;
}

int fake_join_path(lua_State* L)
{
  lua_pushfstring(L, "%s/%s", luaL_checkstring(L, 1), luaL_checkstring(L, 2));
  return 1;
}

// Globals like the app's: Process, app with app.fs, and the debug
// library without the functions the engine takes away
void register_app_like_globals(lua_State* L)
{
  luaL_newmetatable(L, "FakeProcess");
  lua_newtable(L);
  lua_pushcfunction(L, fake_process_start);
  lua_setfield(L, -2, "start");
  lua_setfield(L, -2, "__index");
  lua_pushboolean(L, false);
  lua_setfield(L, -2, "__metatable");
  lua_pop(L, 1);
  lua_register(L, "Process", fake_process_new);

  lua_newtable(L);
  lua_newtable(L);
  lua_pushcfunction(L, fake_join_path);
  lua_setfield(L, -2, "joinPath");
  lua_setfield(L, -2, "fs");
  lua_newtable(L);
  lua_pushboolean(L, false);
  lua_setfield(L, -2, "__metatable");
  lua_setmetatable(L, -2);
  lua_setglobal(L, "app");

  luaL_dostring(L,
                "for _, k in ipairs{ 'getlocal', 'setlocal', 'getupvalue', 'setupvalue',"
                "                    'upvaluejoin', 'sethook', 'getregistry', 'getmetatable',"
                "                    'setmetatable', 'getuservalue', 'setuservalue' } do"
                "  debug[k] = nil "
                "end");
}

// A Lua state with probe() and the trusted chunk of a file whose
// functions call it: direct() and through(f), which calls f first
class Trust : public ::testing::Test {
protected:
  void SetUp() override
  {
    m_file = (std::filesystem::temp_directory_path() / "trust_tests_trusted.lua").string();
    std::ofstream(m_file) << "function direct() return (probe()) end\n"
                             "function through(f) f() return (probe()) end\n"
                             "function callback(f) return (f()) end\n"
                             "trustedLoad = probe()\n";
    L = luaL_newstate();
    luaL_openlibs(L);
    lua_register(L, "probe", probe);
    lua_pushstring(L, m_file.c_str());
    lua_setglobal(L, "trustedFile");
    register_app_like_globals(L);
    g_starts.clear();
    prepare_trust(L);

    // The trusted chunk defines its functions in its own environment;
    // the globals get them too, for the tests to call
    ASSERT_EQ(LUA_OK, luaL_loadfilex(L, m_file.c_str(), "t"));
    trust_chunk(L, -1);
    lua_pushvalue(L, -2);
    ASSERT_EQ(LUA_OK, lua_pcall(L, 0, 0, 0));
    for (const char* name : { "direct", "through", "callback", "trustedLoad" }) {
      lua_getfield(L, -1, name);
      lua_setglobal(L, name);
    }
    lua_pop(L, 2);
  }

  void TearDown() override
  {
    lua_close(L);
    std::remove(m_file.c_str());
  }

  // Runs code as an untrusted chunk and returns what it returns, a
  // boolean, or fails with the error
  bool run(const std::string& code, const std::string& name = "=untrusted")
  {
    if (luaL_loadbuffer(L, code.c_str(), code.size(), name.c_str()) != LUA_OK ||
        lua_pcall(L, 0, 1, 0) != LUA_OK) {
      ADD_FAILURE() << lua_tostring(L, -1);
      lua_pop(L, 1);
      return false;
    }
    const bool result = lua_toboolean(L, -1);
    lua_pop(L, 1);
    return result;
  }

  lua_State* L = nullptr;
  std::string m_file;
};

} // anonymous namespace

TEST_F(Trust, TheTrustedChunkAndItsFunctionsAreTrusted)
{
  EXPECT_TRUE(run("return trustedLoad"));
  lua_getglobal(L, "direct");
  ASSERT_EQ(LUA_OK, lua_pcall(L, 0, 1, 0));
  EXPECT_TRUE(lua_toboolean(L, -1));
  lua_pop(L, 1);
}

TEST_F(Trust, AnUntrustedChunkIsNotTrusted)
{
  EXPECT_FALSE(run("return (probe())"));
}

TEST_F(Trust, AnUntrustedCallerOfATrustedFunctionIsNotTrusted)
{
  EXPECT_FALSE(run("return (direct())"));
}

TEST_F(Trust, ATailCallFromAnUntrustedChunkIsNotTrusted)
{
  EXPECT_FALSE(run("return direct()"));
}

TEST_F(Trust, AnUntrustedFunctionCalledByATrustedOneIsNotTrusted)
{
  EXPECT_FALSE(run("return callback(function() return (probe()) end)"));
}

TEST_F(Trust, ACoroutineIsNotTrusted)
{
  EXPECT_FALSE(run("return coroutine.wrap(direct)()"));
}

TEST_F(Trust, AChunkWithTheNameOfTheTrustedFileIsNotTrusted)
{
  EXPECT_FALSE(run("return (probe())", "@" + m_file));
  EXPECT_FALSE(run("return load('return (probe())', '@' .. trustedFile)()"));
}

TEST_F(Trust, TheTrustedFileLoadedAgainIsNotTrusted)
{
  EXPECT_FALSE(run("dofile(trustedFile) return trustedLoad"));
  EXPECT_FALSE(run("return (direct())"));
}

TEST_F(Trust, AScriptCantSteerALaterTrustedChunkThroughTheGlobals)
{
  // What a script of a user's extension could do before the trusted
  // chunk loads
  EXPECT_TRUE(run(R"(
    local realProcess, fs = Process, app.fs
    for _, attack in ipairs{
      function() Process = function() return realProcess{ "/bin/sh" } end end,
      function() fs.joinPath = function() return "/bin/sh" end end,
      function() rawset(app, "fs", { joinPath=function() return "/bin/sh" end }) end,
      function() string.lower = function() return "sh" end end,
      function() string.format = function() return "sh" end end,
      function() getmetatable("").__index = { lower=function() return "sh" end } end,
      function() getmetatable("").__metatable = nil end,
      function() getmetatable(io.stdout).__index.write = function() end end,
    } do pcall(attack) end
    return true)"));

  const std::string plugin =
    "Process{ app.fs.joinPath('/usr/bin', string.format('%s', ('TRUE'):lower())) }:start()";
  ASSERT_EQ(LUA_OK, luaL_loadbuffer(L, plugin.c_str(), plugin.size(), "=plugin"));
  trust_chunk(L, -1);
  lua_pop(L, 1);
  ASSERT_EQ(LUA_OK, lua_pcall(L, 0, 0, 0)) << lua_tostring(L, -1);
  ASSERT_EQ(1u, g_starts.size());
  EXPECT_EQ("/usr/bin/true", g_starts[0].program);
  EXPECT_TRUE(g_starts[0].trusted);
}

TEST_F(Trust, AScriptCantChangeTheMetatablesOfStringsAndFiles)
{
  EXPECT_TRUE(run("return getmetatable('') == false and getmetatable(io.stdout) == false"));
  EXPECT_TRUE(run("string.upper = function() return 'x' end return ('a'):upper() == 'A'"));
  // A method a script adds to the string table still works
  EXPECT_TRUE(run("function string.twice(s) return s .. s end return ('a'):twice() == 'aa'"));
}

TEST_F(Trust, AScriptGetsNoFunctionOfTheStack)
{
  EXPECT_TRUE(run("local a, b "
                  "callback(function() "
                  "  a = debug.getinfo(2, 'fS') "
                  "  b = debug.getinfo(coroutine.running(), 2, 'f') "
                  "end) "
                  "return a.func == nil and a.source ~= nil and b.func == nil"));
  EXPECT_TRUE(run("return debug.getinfo(print, 'f').func == print"));
}
