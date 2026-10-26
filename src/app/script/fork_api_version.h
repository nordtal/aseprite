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
// API_VERSION, which belongs to Aseprite. A restriction isn't an
// addition and doesn't raise it; these hold at every version:
// - Every loader takes text chunks only: load(), loadfile(),
//   dofile(), require() and the scripts the app runs refuse
//   precompiled bytecode, and a mode argument that asks for "b".
// - require() looks for C modules only in the package.cpath the app
//   started with, whatever a script sets it to, and loads one only
//   with the access to load libraries, like package.loadlib().
//
// 1: Process, app.fs.chooseFolder(), Dialog:tree{} and Dialog:modifyNode{}
// 2: Dialog{ tab=true, closable } and Dialog:editor{ sprite, onchange }
// 3: Process{ env }
// 4: app.fs.removeTree(), require() in a trusted chunk of a file and
//    plugin.require() of the season plugin
// 5: Dialog:tree{ width } and Dialog:modify{ id, width } of a tree
#define FORK_API_VERSION 5

#endif
