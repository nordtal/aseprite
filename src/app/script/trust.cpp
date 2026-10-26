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
#include "lstate.h"
#include "lualib.h"

#include <algorithm>
#include <filesystem>
#include <string>

namespace app { namespace script {

namespace {

// The registry table of trusted prototypes (light userdata keys)
const char kTrusted = 0;

// The registry table of the trusted chunks by their source. Each keeps
// its prototypes alive, so no other prototype takes their address
// while they're trusted. A chunk from the same source replaces it, and
// its prototypes stop being trusted before it's let go.
const char kChunks = 0;

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

// Trusts p and the prototypes in it, or stops trusting them, in the
// table on top of the stack
void trust_proto(lua_State* L, const Proto* p, bool trusted = true)
{
  if (trusted)
    lua_pushboolean(L, true);
  else
    lua_pushnil(L);
  lua_rawsetp(L, -2, p);
  for (int i = 0; i < p->sizep; ++i)
    trust_proto(L, p->p[i], trusted);
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
  return luaL_error(L, "a trusted chunk that isn't a file has no modules");
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

namespace {

// Trusts the chunk at idx in place of the earlier one from its source,
// with the environment at env
void trust(lua_State* L, int idx, int env)
{
  idx = lua_absindex(L, idx);
  env = lua_absindex(L, env);
  const Proto* p = proto_of(L, idx);
  lua_pushvalue(L, env);
  lua_setupvalue(L, idx, 1);

  if (lua_rawgetp(L, LUA_REGISTRYINDEX, &kChunks) != LUA_TTABLE) {
    lua_pop(L, 1);
    lua_newtable(L);
    lua_pushvalue(L, -1);
    lua_rawsetp(L, LUA_REGISTRYINDEX, &kChunks);
  }
  const int chunks = lua_gettop(L);
  const char* source = (p->source ? getstr(p->source) : "=?");
  push_trusted(L);
  if (const Proto* earlier = (lua_getfield(L, chunks, source) == LUA_TFUNCTION ?
                                proto_of(L, -1) :
                                nullptr)) {
    lua_pop(L, 1);
    trust_proto(L, earlier, false);
  }
  else {
    lua_pop(L, 1);
  }
  trust_proto(L, p);
  lua_pushvalue(L, idx);
  lua_setfield(L, chunks, source);
  lua_settop(L, chunks - 1);
}

// The file of the module name inside folder: the parts of the name
// between its dots are folders, the last one a .lua file, and the
// file, links followed, must lie inside the folder
bool module_file(const std::string& folder, const std::string& name, std::string& file)
{
  namespace fs = std::filesystem;
  fs::path path = fs::u8path(folder);
  size_t begin = 0;
  while (true) {
    const size_t end = std::min(name.find('.', begin), name.size());
    const std::string part = name.substr(begin, end - begin);
    if (part.empty() || part.find_first_not_of("abcdefghijklmnopqrstuvwxyz"
                                               "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                               "0123456789_-") != std::string::npos)
      return false;
    if (end == name.size()) {
      path /= fs::u8path(part + ".lua");
      break;
    }
    path /= fs::u8path(part);
    begin = end + 1;
  }

  std::error_code ec;
  const fs::path root = fs::canonical(fs::u8path(folder), ec);
  if (ec)
    return false;
  const fs::path real = fs::canonical(path, ec);
  if (ec || !fs::is_regular_file(real, ec))
    return false;
  auto r = root.begin();
  auto f = real.begin();
  for (; r != root.end(); ++r, ++f) {
    if (f == real.end() || *r != *f)
      return false;
  }
  file = real.u8string();
  return true;
}

// require(name) of a trusted chunk: loads the module name from the
// chunk's folder (see module_file()) once, as a trusted text chunk in
// the chunk's environment, and returns what it returns. The upvalues
// are the folder, the environment and the loaded modules.
int trusted_require(lua_State* L)
{
  const std::string name = luaL_checkstring(L, 1);
  if (!is_trusted_call(L))
    return luaL_error(L, "only a script that comes with the app can require a trusted module");
  lua_settop(L, 1);
  if (lua_getfield(L, lua_upvalueindex(3), name.c_str()) != LUA_TNIL)
    return 1;
  lua_pop(L, 1);

  const char* folder = lua_tostring(L, lua_upvalueindex(1));
  std::string file;
  if (!module_file(folder, name, file))
    return luaL_error(L, "there is no module '%s' in %s", name.c_str(), folder);
  if (luaL_loadfilex(L, file.c_str(), "t") != LUA_OK)
    return lua_error(L);
  trust(L, -1, lua_upvalueindex(2));
  lua_pushvalue(L, 1);
  lua_call(L, 1, 1);
  if (lua_isnil(L, -1)) {
    lua_pop(L, 1);
    lua_pushboolean(L, true);
  }
  lua_pushvalue(L, -1);
  lua_setfield(L, lua_upvalueindex(3), name.c_str());
  return 1;
}

} // anonymous namespace

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
  const int env = lua_gettop(L);

  // The chunk of a file gets the modules of its folder
  const std::string source = (p->source ? getstr(p->source) : "");
  const size_t slash = source.find_last_of("/\\");
  if (source[0] == '@' && slash != std::string::npos) {
    lua_pushlstring(L, source.c_str() + 1, slash - 1);
    lua_pushvalue(L, env);
    lua_newtable(L);
    lua_pushcclosure(L, trusted_require, 3);
    lua_setfield(L, env, "require");
  }

  trust(L, idx, env);
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
    // The collector runs a finalizer on top of whatever it interrupted,
    // even the app with no Lua function below
    if (ar.i_ci->previous && (ar.i_ci->previous->callstatus & CIST_FIN)) {
      trusted = false;
      break;
    }
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
