// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_SCRIPT_TRUST_H_INCLUDED
#define APP_SCRIPT_TRUST_H_INCLUDED
#pragma once

#include "lua.h"

namespace app { namespace script {

// Trust belongs to the functions of a chunk that the app itself
// loaded from one of its own files, not to a file name: a chunk that a
// script loads, even from the same file or under the same name, is
// another chunk and isn't trusted.

// Trusts the chunk at index idx and every function defined in it.
// The chunk stays alive with the Lua state.
void trust_chunk(lua_State* L, int idx);

// True when the call runs on the main thread and every Lua function
// on its stack is trusted. A Lua function that was tail called counts
// as untrusted, as its caller is gone from the stack.
bool is_trusted_call(lua_State* L);

}} // namespace app::script

#endif
