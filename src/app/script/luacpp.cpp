// Aseprite
// Copyright (C) 2018-2023  Igara Studio S.A.
// Copyright (C) 2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/script/luacpp.h"

#include <cstdio>

namespace app { namespace script {

namespace {

// Looks name up in the metatable of t, or calls its getter or setter.
// It's C, so a script that replaces a global function can't change
// how the app's objects find their methods and properties.
int generic_mt_access(lua_State* L, const bool set)
{
  const int t = 1, k = 2;
  if (!lua_getmetatable(L, t))
    return 0;
  const int mt = lua_gettop(L);
  lua_pushvalue(L, k);
  if (lua_rawget(L, mt) != LUA_TNIL)
    return 1;
  lua_pop(L, 1);
  if (lua_getfield(L, mt, set ? "__setters" : "__getters") == LUA_TTABLE) {
    lua_pushvalue(L, k);
    if (lua_rawget(L, -2) != LUA_TNIL) {
      lua_pushvalue(L, t);
      if (set) {
        lua_pushvalue(L, 3);
        lua_call(L, 2, 0);
        return 0;
      }
      lua_call(L, 1, 1);
      return 1;
    }
  }
  return -1;
}

int generic_mt_index(lua_State* L)
{
  const int n = generic_mt_access(L, false);
  if (n >= 0)
    return n;
  if (lua_type(L, 1) == LUA_TTABLE) {
    lua_pushvalue(L, 2);
    lua_rawget(L, 1);
    return 1;
  }
  luaL_traceback(L, L, nullptr, 1);
  // The key's string is pushed first, then the traceback is at -2
  const char* key = luaL_tolstring(L, 2, nullptr);
  return luaL_error(L, "%s: Field '%s' does not exist", lua_tostring(L, -2), key);
}

int generic_mt_newindex(lua_State* L)
{
  const int n = generic_mt_access(L, true);
  if (n >= 0)
    return n;
  if (lua_type(L, 1) == LUA_TTABLE) {
    lua_settop(L, 3);
    lua_rawset(L, 1);
    return 0;
  }
  luaL_traceback(L, L, nullptr, 1);
  // The key's string is pushed first, then the traceback is at -2
  const char* key = luaL_tolstring(L, 2, nullptr);
  return luaL_error(L, "%s: Cannot set field '%s'", lua_tostring(L, -2), key);
}

} // anonymous namespace

void set_generic_mt_functions(lua_State* L, int mt)
{
  mt = lua_absindex(L, mt);
  lua_pushcfunction(L, generic_mt_index);
  lua_setfield(L, mt, "__index");
  lua_pushcfunction(L, generic_mt_newindex);
  lua_setfield(L, mt, "__newindex");
  // getmetatable() gives a script false instead of the metatable, so
  // it can't change the methods of the objects other scripts use
  lua_pushboolean(L, false);
  lua_setfield(L, mt, "__metatable");
}

void create_mt_getters_setters(lua_State* L, const char* tname, const Property* properties)
{
#ifdef _DEBUG
  const int top = lua_gettop(L);
#endif

  bool withGetters = false;
  bool withSetters = false;
  for (auto p = properties; p->name; ++p) {
    if (p->getter)
      withGetters = true;
    if (p->setter)
      withSetters = true;
  }
  ASSERT(withGetters || withSetters);

  luaL_getmetatable(L, tname);
  if (withGetters) {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "__getters");
    for (auto p = properties; p->name; ++p) {
      if (p->getter) {
        lua_pushcclosure(L, p->getter, 0);
        lua_setfield(L, -2, p->name);
      }
    }
    lua_pop(L, 1);
  }
  if (withSetters) {
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_setfield(L, -3, "__setters");
    for (auto p = properties; p->name; ++p) {
      if (p->setter) {
        lua_pushcclosure(L, p->setter, 0);
        lua_setfield(L, -2, p->name);
      }
    }
    lua_pop(L, 1);
  }
  lua_pop(L, 1);

  ASSERT(lua_gettop(L) == top);
}

bool lua_is_key_true(lua_State* L, int tableIndex, const char* keyName)
{
  bool result = false;
  int type = lua_getfield(L, tableIndex, keyName);
  if (type != LUA_TNIL && lua_toboolean(L, -1))
    result = true;
  lua_pop(L, 1);
  return result;
}

}} // namespace app::script
