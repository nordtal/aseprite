// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_SETTINGS_IMPORT_H_INCLUDED
#define APP_SETTINGS_IMPORT_H_INCLUDED
#pragma once

#include <string>

namespace app {

// The config directory of an installed Aseprite, if it has something
// that import_aseprite_settings() copies, or an empty string.
std::string find_aseprite_settings();

// True if the given directory has preferences, palettes or shortcuts.
bool has_aseprite_settings(const std::string& dir);

// Copies the preferences, palettes and shortcuts from the config
// directory of Aseprite to the given one. Extensions stay behind.
void import_aseprite_settings(const std::string& from, const std::string& to);

// Asks whether to copy the settings of the Aseprite in the given
// directory, in a dialog of the OS, as the app's UI doesn't exist yet.
bool ask_to_import_aseprite_settings(const std::string& from);

} // namespace app

#endif
