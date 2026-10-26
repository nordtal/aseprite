// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/script/trust.h"

#include "lobject.h"

namespace app { namespace script {

namespace {

// The registry table of trusted prototypes (light userdata keys) and
// of the trusted chunks, which keep their prototypes alive so no other
// prototype takes their address
const char kTrusted = 0;

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

} // anonymous namespace

void trust_chunk(lua_State* L, int idx)
{
  idx = lua_absindex(L, idx);
  const Proto* p = proto_of(L, idx);
  if (!p)
    return;
  push_trusted(L);
  trust_proto(L, p);
  lua_pushvalue(L, idx);
  lua_pushboolean(L, true);
  lua_rawset(L, -3);
  lua_pop(L, 1);
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
  lua_Debug ar;
  for (int level = 0; lua_getstack(L, level, &ar); ++level) {
    lua_getinfo(L, "Stf", &ar);
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
