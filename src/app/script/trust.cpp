// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/script/trust.h"

#include "lauxlib.h"
#include "lobject.h"
#include "lualib.h"

#include <algorithm>
#include <string>

namespace app { namespace script {

namespace {

// The registry table of trusted prototypes (light userdata keys) and
// of the trusted chunks, which keep their prototypes alive so no other
// prototype takes their address
const char kTrusted = 0;

// The registry table with the copy of the globals that prepare_trust()
// makes
const char kGlobals = 0;

// The C functions through which a call runs on another thread, whose
// stack the main thread's doesn't show: coroutine.resume() and the
// function that coroutine.wrap() returns
lua_CFunction g_resume = nullptr;
lua_CFunction g_wrapped = nullptr;

void push_trusted(lua_State* L)
{
  if (lua_rawgetp(L, LUA_REGISTRYINDEX, &kTrusted) != LUA_TTABLE) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kTrusted);
  }
}

void trust_proto(lua_State* L, const Proto* p)
{
  lua_pushboolean(L, true);
  lua_rawsetp(L, -2, p);
  for (int i = 0; i < p->sizep; ++i)
    trust_proto(L, p->p[i]);
}

const Proto* proto_of(lua_State* L, int idx)
{
  if (!lua_isfunction(L, idx) || lua_iscfunction(L, idx))
    return nullptr;
  return static_cast<const LClosure*>(lua_topointer(L, idx))->p;
}

// debug.getinfo() without the function of a stack level: with a
// level, 'f' is dropped from the fields asked for. The original
// function is the upvalue; the level is one more, for this function.
int getinfo_without_functions(lua_State* L)
{
  const int arg = (lua_isthread(L, 1) ? 2 : 1);
  if (!lua_isfunction(L, arg)) {
    std::string what = luaL_optstring(L, arg + 1, "flnSrtu");
    what.erase(std::remove(what.begin(), what.end(), 'f'), what.end());
    const bool here = (arg == 1 || lua_tothread(L, 1) == L);
    if (here && lua_isinteger(L, arg)) {
      const lua_Integer level = lua_tointeger(L, arg);
      lua_pushinteger(L, level + 1);
      lua_replace(L, arg);
    }
    lua_settop(L, arg);
    lua_pushstring(L, what.c_str());
  }
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_insert(L, 1);
  lua_call(L, lua_gettop(L) - 1, 1);
  return 1;
}

int no_require(lua_State* L)
{
  return luaL_error(L, "a trusted script loads its files with plugin:loadTrusted()");
}

void copy_fields(lua_State* L, int from, int to, int memo);

// Pushes a copy of the table at idx, with the same metatable; memo
// maps each table copied so far to its copy
void push_copy(lua_State* L, int idx, int memo)
{
  idx = lua_absindex(L, idx);
  lua_pushvalue(L, idx);
  if (lua_rawget(L, memo) != LUA_TNIL)
    return;
  lua_pop(L, 1);
  lua_newtable(L);
  lua_pushvalue(L, idx);
  lua_pushvalue(L, -2);
  lua_rawset(L, memo);
  if (lua_getmetatable(L, idx))
    lua_setmetatable(L, -2);
  copy_fields(L, idx, lua_gettop(L), memo);
}

// Copies the fields of the table from into the table to: a table as a
// copy, a Lua function not at all, as it would run with the globals
// scripts share
void copy_fields(lua_State* L, int from, int to, int memo)
{
  lua_pushnil(L);
  while (lua_next(L, from)) {
    if (lua_isfunction(L, -1) && !lua_iscfunction(L, -1)) {
      lua_pop(L, 1);
      continue;
    }
    if (lua_istable(L, -1)) {
      push_copy(L, -1, memo);
      lua_remove(L, -2);
    }
    lua_pushvalue(L, -2);
    lua_insert(L, -2);
    lua_rawset(L, to);
  }
}

// Pushes a copy of the table at idx made by push_copy()
void push_copy(lua_State* L, int idx)
{
  idx = lua_absindex(L, idx);
  lua_newtable(L);
  push_copy(L, idx, lua_gettop(L));
  lua_remove(L, -2);
}

} // anonymous namespace

void prepare_trust(lua_State* L)
{
  // Strings: a copy of the string library for their methods, which
  // falls back on the library for the methods scripts add to it
  lua_pushliteral(L, "");
  lua_getmetatable(L, -1);
  lua_getfield(L, -1, "__index");
  const int lib = lua_gettop(L);
  lua_newtable(L);
  lua_pushnil(L);
  while (lua_next(L, lib)) {
    lua_pushvalue(L, -2);
    lua_insert(L, -2);
    lua_rawset(L, -4);
  }
  lua_newtable(L);
  lua_pushvalue(L, lib);
  lua_setfield(L, -2, "__index");
  lua_pushboolean(L, false);
  lua_setfield(L, -2, "__metatable");
  lua_setmetatable(L, -2);
  lua_setfield(L, -3, "__index");
  lua_pop(L, 1);
  lua_pushboolean(L, false);
  lua_setfield(L, -2, "__metatable");
  lua_pop(L, 2);

  // Files
  luaL_getmetatable(L, LUA_FILEHANDLE);
  if (lua_istable(L, -1)) {
    lua_pushboolean(L, false);
    lua_setfield(L, -2, "__metatable");
  }
  lua_pop(L, 1);

  lua_getglobal(L, LUA_COLIBNAME);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "resume");
    g_resume = lua_tocfunction(L, -1);
    lua_getfield(L, -2, "wrap");
    luaL_loadstring(L, "");
    lua_call(L, 1, 1);
    g_wrapped = lua_tocfunction(L, -1);
    lua_pop(L, 2);
  }
  lua_pop(L, 1);

  lua_getglobal(L, LUA_DBLIBNAME);
  if (lua_istable(L, -1)) {
    lua_getfield(L, -1, "getinfo");
    lua_pushcclosure(L, getinfo_without_functions, 1);
    lua_setfield(L, -2, "getinfo");
  }
  lua_pop(L, 1);

  // The copy of the globals, without the module loader of the scripts
  lua_pushglobaltable(L);
  push_copy(L, -1);
  for (const char* name : { LUA_LOADLIBNAME, "require" }) {
    lua_pushnil(L);
    lua_setfield(L, -2, name);
  }
  lua_pushcfunction(L, no_require);
  lua_setfield(L, -2, "require");
  lua_rawsetp(L, LUA_REGISTRYINDEX, &kGlobals);
  lua_pop(L, 1);
}

void trust_chunk(lua_State* L, int idx)
{
  idx = lua_absindex(L, idx);
  const Proto* p = proto_of(L, idx);
  // Without the copy of the globals a chunk can't run trusted
  if (!p || lua_rawgetp(L, LUA_REGISTRYINDEX, &kGlobals) != LUA_TTABLE) {
    if (p)
      lua_pop(L, 1);
    lua_pushnil(L);
    return;
  }
  push_copy(L, -1);
  lua_remove(L, -2);
  lua_pushvalue(L, -1);
  lua_setupvalue(L, idx, 1);
  const int env = lua_gettop(L);
  push_trusted(L);
  trust_proto(L, p);
  lua_pushvalue(L, idx);
  lua_pushboolean(L, true);
  lua_rawset(L, -3);
  lua_pop(L, 1);
  lua_settop(L, env);
}

bool is_trusted_call(lua_State* L)
{
  if (!lua_pushthread(L)) {
    lua_pop(L, 1);
    return false;
  }
  lua_pop(L, 1);

  push_trusted(L);
  bool trusted = false;
  lua_Debug ar, caller;
  for (int level = 0; lua_getstack(L, level, &ar); ++level) {
    lua_getinfo(L, "Stf", &ar);
    if (lua_iscfunction(L, -1)) {
      // A C function on the stack hides what it was called from when
      // the app called it, or when it runs a coroutine
      const lua_CFunction f = lua_tocfunction(L, -1);
      lua_pop(L, 1);
      const bool calledFromLua = lua_getstack(L, level + 1, &caller) &&
                                 lua_getinfo(L, "S", &caller) && *caller.what != 'C';
      if (f == g_resume || f == g_wrapped || (level > 0 && !calledFromLua)) {
        trusted = false;
        break;
      }
      continue;
    }
    const Proto* p = proto_of(L, -1);
    lua_pop(L, 1);
    if (!p)
      continue;
    if (ar.istailcall || lua_rawgetp(L, -1, p) != LUA_TBOOLEAN) {
      trusted = false;
      lua_pop(L, 1);
      break;
    }
    lua_pop(L, 1);
    trusted = true;
  }
  lua_pop(L, 1);
  return trusted;
}

}} // namespace app::script
