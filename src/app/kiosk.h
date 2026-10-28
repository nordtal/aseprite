// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_KIOSK_H_INCLUDED
#define APP_KIOSK_H_INCLUDED
#pragma once

namespace app {
class Command;

// The kiosk: the window shows only what the scripts that come with the
// app put into it. Once started it lasts for the session:
// - The Home tab closes and never opens again.
// - A document gets no tab of its own. It shows only where a tab
//   dialog embeds it, and in a tab that asks about its changes as the
//   app quits.
// - The window takes no drops of files, images or extensions, and the
//   app opens no file the system hands it.
// - Commands that open, create or close documents or tabs, or move
//   between tabs, are disabled, unless a kiosk::Allow lets them run.
namespace kiosk {

bool active();
void start();

// True when the command is disabled in the kiosk
bool refuses(const Command* command);

// While one lives, the kiosk lets the one disabled command run: a
// trusted script opens a file, or the app closes every document as it
// quits
class Allow {
public:
  explicit Allow(const char* commandId);
  ~Allow();
  Allow(const Allow&) = delete;
  Allow& operator=(const Allow&) = delete;
};

} // namespace kiosk
} // namespace app

#endif
