// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_SCRIPT_FORK_API_VERSION_H_INCLUDED
#define APP_SCRIPT_FORK_API_VERSION_H_INCLUDED
#pragma once

// Version of the Lua API that Nordtal Designer adds to Aseprite's, as
// app.forkApiVersion. It rises with every addition, independently of
// API_VERSION, which belongs to Aseprite.
//
// 1: Process, app.fs.chooseFolder(), Dialog:tree{} and Dialog:modifyNode{}
// 2: Dialog{ tab=true, closable } and Dialog:editor{ sprite, onchange }
// 3: Process{ env }
// 4: app.fs.removeTree(), require() in a trusted chunk of a file and
//    plugin.require() of the season plugin
#define FORK_API_VERSION 4

#endif
