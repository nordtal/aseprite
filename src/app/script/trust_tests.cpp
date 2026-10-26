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

using namespace app::script;

namespace {

int probe(lua_State* L)
{
  lua_pushboolean(L, is_trusted_call(L));
  return 1;
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
    ASSERT_EQ(LUA_OK, luaL_loadfilex(L, m_file.c_str(), "t"));
    trust_chunk(L, -1);
    ASSERT_EQ(LUA_OK, lua_pcall(L, 0, 0, 0));
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
