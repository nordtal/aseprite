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
#include <functional>
#include <string>
#include <vector>

using namespace app::script;

namespace {

// Whether each call of probe() was trusted
std::vector<bool> g_probes;

int probe(lua_State* L)
{
  g_probes.push_back(is_trusted_call(L));
  lua_pushboolean(L, g_probes.back());
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

// access(mode, resource): whether the call gets the access mode to
// the resource without asking
int access(lua_State* L)
{
  lua_pushboolean(L,
                  is_trusted_access(L,
                                    FileAccessMode(luaL_checkinteger(L, 1)),
                                    luaL_checkstring(L, 2)));
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
                             "function callback(f, ...) return (f(...)) end\n"
                             "trustedLoad = probe()\n";
    L = luaL_newstate();
    luaL_openlibs(L);
    lua_register(L, "probe", probe);
    lua_register(L, "access", access);
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

  // Whether the trusted callback() gets the access mode to resource
  // without asking
  bool trustedAccess(FileAccessMode mode, const char* resource)
  {
    lua_getglobal(L, "callback");
    lua_getglobal(L, "access");
    lua_pushinteger(L, int(mode));
    lua_pushstring(L, resource);
    if (lua_pcall(L, 3, 1, 0) != LUA_OK) {
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

TEST_F(Trust, ATrustedCallReadsWritesAndRunsWithoutAsking)
{
  for (FileAccessMode mode : { FileAccessMode::Read, FileAccessMode::Write, FileAccessMode::Execute })
    EXPECT_TRUE(trustedAccess(mode, "/anywhere")) << int(mode);
  EXPECT_FALSE(trustedAccess(FileAccessMode::LoadLib, "/anywhere"));
}

#ifdef ENABLE_WEBSOCKET
TEST_F(Trust, ATrustedCallOpensAWebSocketToTheLoopbackHostOnlyWithoutAsking)
{
  for (const char* url :
       { "ws://127.0.0.1:4711", "ws://127.0.0.1:4711/bridge?x=1", "ws://localhost:4711", "ws://localhost" })
    EXPECT_TRUE(trustedAccess(FileAccessMode::OpenSocket, url)) << url;

  for (const char* url : { "ws://example.com:4711",
                           "ws://127.0.0.1.example.com:4711",
                           "ws://localhost.example.com",
                           "ws://localhost@example.com:4711",
                           "ws://127.0.0.1:4711@example.com",
                           "ws://example.com/127.0.0.1",
                           "ws://127.0.0.2:4711",
                           "ws://[::1]:4711",
                           "127.0.0.1:4711",
                           "" })
    EXPECT_FALSE(trustedAccess(FileAccessMode::OpenSocket, url)) << url;
}

#endif

TEST_F(Trust, AnUntrustedCallOpensNoWebSocketWithoutAsking)
{
  EXPECT_FALSE(run("return (access(8, 'ws://127.0.0.1:4711'))"));
  EXPECT_FALSE(run("return (access(4, '/anywhere'))"));
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

// A command of the app that runs the trusted direct() on the main
// thread, as a plugin command runs its onclick, and keeps in the
// global "commanded" whether that call was trusted
int command(lua_State* L)
{
  lua_rawgeti(L, LUA_REGISTRYINDEX, LUA_RIDX_MAINTHREAD);
  lua_State* main = lua_tothread(L, -1);
  lua_pop(L, 1);
  lua_getglobal(main, "direct");
  const bool trusted = (lua_pcall(main, 0, 1, 0) == LUA_OK && lua_toboolean(main, -1));
  lua_pop(main, 1);
  lua_pushboolean(main, trusted);
  lua_setglobal(main, "commanded");
  return 0;
}

// Calls the function below its nargs arguments from the app, as a
// timer calls its ontick, and returns whether the command it runs was
// trusted
bool call_back(lua_State* L, int nargs = 0)
{
  lua_pushnil(L);
  lua_setglobal(L, "commanded");
  if (lua_pcall(L, nargs, 0, 0) != LUA_OK) {
    ADD_FAILURE() << lua_tostring(L, -1);
    lua_pop(L, 1);
    return false;
  }
  lua_getglobal(L, "commanded");
  EXPECT_TRUE(lua_isboolean(L, -1)) << "the command didn't run";
  const bool result = lua_toboolean(L, -1);
  lua_pop(L, 1);
  return result;
}

TEST_F(Trust, ACommandThatACoroutineRunsIsNotTrusted)
{
  lua_register(L, "command", command);
  // The command itself runs trusted code
  lua_getglobal(L, "callback");
  lua_getglobal(L, "command");
  EXPECT_TRUE(call_back(L, 1));

  // The app calls a coroutine back, and the coroutine runs the
  // command: the stack of the main thread shows no untrusted function
  ASSERT_EQ(LUA_OK, luaL_dostring(L, "return coroutine.wrap(function() command() end)"));
  EXPECT_FALSE(call_back(L));

  // The same, through a trusted function the app calls back with the
  // coroutine
  lua_getglobal(L, "callback");
  ASSERT_EQ(LUA_OK, luaL_dostring(L, "return coroutine.wrap(function() command() end)"));
  EXPECT_FALSE(call_back(L, 1));

  lua_getglobal(L, "callback");
  lua_getglobal(L, "coroutine");
  lua_getfield(L, -1, "resume");
  lua_remove(L, -2);
  ASSERT_EQ(LUA_OK, luaL_dostring(L, "return coroutine.create(function() command() end)"));
  EXPECT_FALSE(call_back(L, 2));
}

TEST_F(Trust, ACFunctionThatTheAppCallsBackIsNotTrusted)
{
  lua_pushcfunction(L, command);
  EXPECT_FALSE(call_back(L));
}

TEST_F(Trust, ATrustedFunctionThatRunsAsAFinalizerIsNotTrusted)
{
  // A script makes the trusted direct() the finalizer of a table, and
  // the collector calls it when the app or a trusted function makes
  // the step that collects it: no untrusted function is on the stack
  const auto collectedBy = [this](const std::function<void()>& step) {
    run("setmetatable({}, { __gc=direct })");
    g_probes.clear();
    step();
    EXPECT_EQ(1u, g_probes.size()) << "the finalizer didn't run";
    return !g_probes.empty() && g_probes[0];
  };
  EXPECT_FALSE(collectedBy([this] { lua_gc(L, LUA_GCCOLLECT); }));
  EXPECT_FALSE(collectedBy([this] {
    lua_getglobal(L, "callback");
    lua_getglobal(L, "collectgarbage");
    ASSERT_EQ(LUA_OK, lua_pcall(L, 1, 0, 0)) << lua_tostring(L, -1);
  }));
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

TEST_F(Trust, ATrustedFileLoadedAgainReplacesItsEarlierChunk)
{
  const auto load = [this] {
    ASSERT_EQ(LUA_OK, luaL_loadfilex(L, m_file.c_str(), "t"));
    trust_chunk(L, -1);
    lua_pop(L, 2);
  };
  load();
  lua_gc(L, LUA_GCCOLLECT);
  const int before = lua_gc(L, LUA_GCCOUNT);
  for (int i = 0; i < 100; ++i)
    load();
  lua_gc(L, LUA_GCCOLLECT);
  EXPECT_LT(lua_gc(L, LUA_GCCOUNT) - before, 64) << "KiB kept by 100 loads";

  // The functions of the chunks it replaced aren't trusted any more
  lua_getglobal(L, "direct");
  ASSERT_EQ(LUA_OK, lua_pcall(L, 0, 1, 0));
  EXPECT_FALSE(lua_toboolean(L, -1));
  lua_pop(L, 1);
}

namespace {

// A plugin folder with modules, next to a file outside it
class TrustedModules : public ::testing::Test {
protected:
  void SetUp() override
  {
    namespace fs = std::filesystem;
    m_root = fs::temp_directory_path() / "trust_tests_modules";
    fs::remove_all(m_root);
    const fs::path plugin = m_root / "plugin";
    fs::create_directories(plugin / "sub");
    std::ofstream(m_root / "outside.lua") << "Process{ '/outside' }:start()\n";
    std::ofstream(plugin / "sub" / "mod.lua")
      << "loads = (loads or 0) + 1\n"
         "Process{ '/module' }:start()\n"
         "return { name=... }\n";
    fs::create_symlink(m_root / "outside.lua", plugin / "link.lua");
    std::ofstream(plugin / "init.lua")
      << "mod = require('sub.mod')\n"
         "again = require('sub.mod')\n"
         "refused = {}\n"
         "for _, name in ipairs{ '../outside', '..', '.outside', 'sub..mod', 'sub/mod',\n"
         "                       '/outside', '', 'link', 'missing', 'sub.mod.lua' } do\n"
         "  local ok, err = pcall(require, name)\n"
         "  refused[name] = not ok and type(err) == 'string'\n"
         "end\n";

    L = luaL_newstate();
    luaL_openlibs(L);
    register_app_like_globals(L);
    g_starts.clear();
    prepare_trust(L);
    const std::string init = (plugin / "init.lua").string();
    ASSERT_EQ(LUA_OK, luaL_loadfilex(L, init.c_str(), "t"));
    trust_chunk(L, -1);
    lua_insert(L, -2);
    ASSERT_EQ(LUA_OK, lua_pcall(L, 0, 0, 0)) << lua_tostring(L, -1);
    m_env = lua_gettop(L);
  }

  void TearDown() override
  {
    lua_close(L);
    std::filesystem::remove_all(m_root);
  }

  lua_State* L = nullptr;
  std::filesystem::path m_root;
  int m_env = 0;
};

} // anonymous namespace

TEST_F(TrustedModules, AModuleOfThePluginRunsTrustedInItsEnvironmentOnce)
{
  ASSERT_EQ(1u, g_starts.size());
  EXPECT_EQ("/module", g_starts[0].program);
  EXPECT_TRUE(g_starts[0].trusted);

  lua_getfield(L, m_env, "loads");
  EXPECT_EQ(1, lua_tointeger(L, -1));
  lua_getfield(L, m_env, "mod");
  lua_getfield(L, m_env, "again");
  EXPECT_TRUE(lua_istable(L, -1) && lua_rawequal(L, -1, -2));
  lua_getfield(L, -1, "name");
  EXPECT_STREQ("sub.mod", lua_tostring(L, -1));
  lua_pop(L, 4);
}

TEST_F(TrustedModules, ANameOutsideThePluginFolderIsRefused)
{
  lua_getfield(L, m_env, "refused");
  for (const char* name : { "../outside", "..", ".outside", "sub..mod", "sub/mod", "/outside", "",
                            "link", "missing", "sub.mod.lua" }) {
    lua_getfield(L, -1, name);
    EXPECT_TRUE(lua_toboolean(L, -1)) << name;
    lua_pop(L, 1);
  }
  lua_pop(L, 1);
  for (const auto& start : g_starts)
    EXPECT_NE("/outside", start.program);
}

TEST_F(TrustedModules, AnUntrustedCallCantRequireAModule)
{
  lua_getfield(L, m_env, "require");
  lua_pushstring(L, "sub.mod");
  EXPECT_NE(LUA_OK, lua_pcall(L, 1, 1, 0));
  lua_pop(L, 1);
}
