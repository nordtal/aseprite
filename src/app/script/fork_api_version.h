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
// 6: Dialog:panel{ id, side, width }, after which the widgets of a
//    Dialog{ tab=true } go into a side panel, where a widget without
//    a label takes the label's column too. The main window docks it
//    at its far edge while the tab is active: side="left" (the
//    default) is outside the colour bar, side="right" outside the
//    tools, both mirrored with the layout. width is in points. A
//    panel with an id keeps the width the user drags it to across
//    restarts, under its id within the plugin; one without an id keeps
//    it for the session under the dialog's title. The id is none of
//    the dialog's other ids. Dialog:modify{ id, side, width } of the
//    panel, and Dialog.bounds of an open tab is the tab's place in the
//    window while it's active, an empty rectangle while it isn't.
// 7: Dialog:splitter{} in a side panel, once at most: the widgets
//    before it go above a splitter that the user drags, the ones after
//    it below. It starts at half the panel's height, and the panel
//    keeps where the user dragged it, as it keeps its width.
// 8: app.kiosk, false until a script sets it to true, which it stays
//    for the session: the Home tab closes, a document shows only in a
//    Dialog{ tab=true }, the window takes no dropped files, and the
//    commands that open, create or close documents or tabs are
//    disabled. Only a trusted script then creates a sprite with
//    Sprite(), opens one with app.open() or Sprite{ fromFile } and
//    opens a Dialog{ tab=true }; any script still reads an image.
#define FORK_API_VERSION 8

#endif
