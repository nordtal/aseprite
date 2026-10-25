// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/settings_import.h"

#include "base/fs.h"
#include "base/string.h"

#if LAF_WINDOWS
  #include <windows.h>
#elif LAF_MACOS
  #include <CoreFoundation/CoreFoundation.h>
#endif

#include <cstdlib>

namespace app {

// What the import copies from the config directory of Aseprite
static const char* kSettings[] = { "aseprite.ini", "user.aseprite-keys", "palettes" };

static void copy_tree(const std::string& from, const std::string& to)
{
  if (base::is_directory(from)) {
    base::make_all_directories(to);
    for (const auto& item : base::list_files(from))
      copy_tree(base::join_path(from, item), base::join_path(to, item));
  }
  else {
    base::copy_file(from, to, true);
  }
}

std::string find_aseprite_settings()
{
  std::string dir;
#if LAF_WINDOWS
  // %AppData%/Aseprite
  if (const wchar_t* env = _wgetenv(L"AppData"))
    dir = base::join_path(base::to_utf8(env), "Aseprite");
#elif LAF_MACOS
  // $HOME/Library/Application Support/Aseprite
  dir = base::join_path(base::get_lib_app_support_path(), "Aseprite");
#endif
  return (!dir.empty() && has_aseprite_settings(dir) ? dir : std::string());
}

bool has_aseprite_settings(const std::string& dir)
{
  for (const char* item : kSettings) {
    const std::string path = base::join_path(dir, item);
    if (base::is_file(path) || base::is_directory(path))
      return true;
  }
  return false;
}

void import_aseprite_settings(const std::string& from, const std::string& to)
{
  for (const char* item : kSettings) {
    const std::string path = base::join_path(from, item);
    if (base::is_file(path) || base::is_directory(path))
      copy_tree(path, base::join_path(to, item));
  }
}

bool ask_to_import_aseprite_settings(const std::string& from)
{
  // The strings aren't loaded yet, so the question is in English
  const std::string title = "Copy the settings of Aseprite?";
  const std::string text =
    "Nordtal Designer found an installed Aseprite in " + from +
    ". It can copy its preferences, palettes and shortcuts, but not its extensions. "
    "After that, each app keeps its own settings.";
#if LAF_WINDOWS
  return MessageBoxW(nullptr,
                     base::from_utf8(text).c_str(),
                     base::from_utf8(title).c_str(),
                     MB_YESNO | MB_ICONQUESTION) == IDYES;
#elif LAF_MACOS
  CFStringRef cfTitle = CFStringCreateWithCString(nullptr, title.c_str(), kCFStringEncodingUTF8);
  CFStringRef cfText = CFStringCreateWithCString(nullptr, text.c_str(), kCFStringEncodingUTF8);
  // The icon of the bundle, if the app runs from one
  CFURLRef icon =
    CFBundleCopyResourceURL(CFBundleGetMainBundle(), CFSTR("Designer"), CFSTR("icns"), nullptr);
  CFOptionFlags response = kCFUserNotificationAlternateResponse;
  CFUserNotificationDisplayAlert(0,
                                 kCFUserNotificationNoteAlertLevel,
                                 icon,
                                 nullptr,
                                 nullptr,
                                 cfTitle,
                                 cfText,
                                 CFSTR("Copy"),
                                 CFSTR("Don't Copy"),
                                 nullptr,
                                 &response);
  if (icon)
    CFRelease(icon);
  CFRelease(cfTitle);
  CFRelease(cfText);
  return response == kCFUserNotificationDefaultResponse;
#else
  return false;
#endif
}

} // namespace app
