// Aseprite
// Copyright (C) 2021-2025  Igara Studio S.A.
// Copyright (C) 2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_SCRIPT_SECURITY_H_INCLUDED
#define APP_SCRIPT_SECURITY_H_INCLUDED
#pragma once

#ifndef ENABLE_SCRIPTING
  #error ENABLE_SCRIPTING must be defined
#endif

#include "app/script/engine.h"
#include "lua.h"

#include <functional>
#include <string>
#include <vector>

namespace app { namespace script {

enum class FileAccessMode {
  Execute = 1,
  Write = 2,
  Read = 4,
  OpenSocket = 8,
  LoadLib = 16,
  Full = Execute | Write | Read | OpenSocket | LoadLib,
};

enum class ResourceType {
  File,
  Command,
  WebSocket,
  Clipboard,
};

void overwrite_unsecure_functions(lua_State* L);

lua_CFunction get_original_io_open();

bool ask_access(lua_State* L,
                const char* filename,
                const FileAccessMode mode,
                const ResourceType resourceType,
                // Generally =2 when the access is requested from a
                // function, or =3 if it's requested from a property.
                const int stackLevel = 2);

// The width the security alert breaks its script name and resource
// at, when its widest one is excess points wider than the window has
// room for. It never falls below 160 points at the UI scale, and the
// window cuts what the alert holds beyond that.
int alert_line_width(int widest, int excess);

// The lines of text, each no wider than width as measure() gives it,
// broken after the last "/" or space that fits, or else after the last
// character that fits. A line holds at least one character.
std::vector<std::string> break_lines(const std::string& text,
                                     int width,
                                     const std::function<int(const std::string&)>& measure);

}} // namespace app::script

#endif
