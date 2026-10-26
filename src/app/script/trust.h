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

// Prepares the Lua state for trusted chunks. It's called once, after
// every global is registered and before any script runs:
// - The metatables of strings and of files can't be changed by a
//   script. The methods of a string come from a copy of the string
//   library, and only a method it lacks from the string table.
// - debug.getinfo() hands out no function of the stack, so a script
//   can't take a trusted function to have the app call it later.
// - The globals are copied as they are now: a trusted chunk runs with
//   these values, not with what a script made of the globals since.
void prepare_trust(lua_State* L);

// Trusts the chunk at index idx and every function defined in it,
// gives it an environment of its own made from the globals that
// prepare_trust() copied, and pushes that environment. The chunk stays
// alive until a chunk from the same source replaces it; then its
// functions aren't trusted any more.
//
// A chunk of a file gets require(name) in its environment: it loads
// <name>.lua from the file's folder, the dots of the name as folders,
// once, and only if the file, links followed, lies inside that folder.
// The module runs trusted in the same environment, as a text chunk;
// only a trusted call can require one.
void trust_chunk(lua_State* L, int idx);

// True when the call runs on the main thread and every Lua function
// on its stack is trusted. A Lua function that was tail called counts
// as untrusted, as its caller is gone from the stack. So does a C
// function on the stack that runs a coroutine, or that no Lua function
// called (the app called it back), as the stack doesn't show what it
// runs for; only the C function that asks is exempt.
bool is_trusted_call(lua_State* L);

}} // namespace app::script

#endif
