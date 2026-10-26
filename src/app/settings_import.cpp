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
#include "cfg/cfg.h"

#if LAF_WINDOWS
  #include <windows.h>
#elif LAF_MACOS
  #include <CoreFoundation/CoreFoundation.h>
#endif

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <vector>

namespace app {

// What the import copies from the config directory of Aseprite
static const char* kSettings[] = { "aseprite.ini", "user.aseprite-keys", "palettes" };

// Sections of aseprite.ini that stay behind: the recent files and
// folders, and the access granted to scripts in Aseprite
static const char* kDroppedSections[] = { "RecentFiles", "RecentPaths", "script_access" };

namespace fs = std::filesystem;

// True if both paths are the same directory, through a symlink or an
// ASEPRITE_USER_FOLDER too. Copying it onto itself would empty its files.
static bool same_directory(const std::string& a, const std::string& b)
{
  std::error_code ec;
  return fs::equivalent(fs::u8path(a), fs::u8path(b), ec);
}

// True if path is dir or lies inside it. Each parent of path is
// compared with dir as a file, so other names of dir count too: a
// symlink, other cases on a file system that ignores them, a mount.
static bool inside(const std::string& dir, const std::string& path)
{
  std::error_code ec;
  const fs::path d = fs::u8path(dir);
  fs::path p = fs::weakly_canonical(fs::u8path(path), ec);
  if (ec)
    return false;
  for (;;) {
    if (fs::equivalent(d, p, ec))
      return true;
    const fs::path parent = p.parent_path();
    if (parent.empty() || parent == p)
      return false;
    p = parent;
  }
}

// The import into "to" copies nothing onto itself or into itself
static bool can_import(const std::string& from, const std::string& to)
{
  return !same_directory(from, to) && !inside(from, to);
}

// Copies a file and lets its owner write the copy, which keeps the
// mode of a read-only source otherwise
static void copy_writable(const fs::path& from, const fs::path& to)
{
  fs::copy_file(from, to, fs::copy_options::overwrite_existing);
  fs::permissions(to, fs::perms::owner_read | fs::perms::owner_write, fs::perm_options::add);
}

// Copies a file or a directory. A symlink to a file is copied as the
// file, and one to a directory isn't followed, so a loop ends.
static void copy_tree(const fs::path& from, const fs::path& to)
{
  const fs::file_status st = fs::symlink_status(from);
  if (fs::is_symlink(st)) {
    if (fs::is_regular_file(fs::status(from)))
      copy_writable(from, to);
  }
  else if (fs::is_directory(st)) {
    fs::create_directories(to);
    for (const auto& entry : fs::directory_iterator(from))
      copy_tree(entry.path(), to / entry.path().filename());
  }
  else if (fs::is_regular_file(st)) {
    copy_writable(from, to);
  }
}

// Deletes the sections of the staged aseprite.ini that stay behind,
// and throws if they are still in the saved file
static void drop_sections(const fs::path& ini)
{
  cfg::CfgFile cfg;
  if (!cfg.load(ini.u8string()))
    throw std::runtime_error("The settings of Aseprite can't be read");
  for (const char* section : kDroppedSections)
    cfg.deleteSection(section);
  cfg.save();

  cfg::CfgFile saved;
  std::vector<std::string> sections;
  if (saved.load(ini.u8string()))
    saved.getAllSections(sections);
  for (const char* section : kDroppedSections) {
    if (std::find(sections.begin(), sections.end(), section) != sections.end())
      throw std::runtime_error("The settings of Aseprite can't be filtered");
  }
}

std::string aseprite_settings_dir()
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
  return dir;
}

std::string find_aseprite_settings(const std::string& from, const std::string& to)
{
  if (from.empty() || !has_aseprite_settings(from) || !can_import(from, to))
    return std::string();
  return from;
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

bool import_aseprite_settings(const std::string& from, const std::string& to)
{
  if (!can_import(from, to))
    return false;

  // Everything is copied and filtered next to the target first, and
  // moved into place once it's complete, so a failed copy leaves
  // nothing, and no script_access, behind
  const fs::path target = fs::u8path(to);
  const fs::path stage = target / ".settings-import";
  fs::remove_all(stage);
  try {
    fs::create_directories(stage);
    for (const char* item : kSettings) {
      const fs::path path = fs::u8path(from) / item;
      if (fs::exists(path))
        copy_tree(path, stage / item);
    }

    const fs::path ini = stage / "aseprite.ini";
    if (fs::is_regular_file(ini))
      drop_sections(ini);

    for (const char* item : kSettings) {
      if (!fs::exists(fs::symlink_status(stage / item)))
        continue;
      fs::remove_all(target / item);
      fs::rename(stage / item, target / item);
    }
    fs::remove_all(stage);
  }
  catch (...) {
    std::error_code ec;
    fs::remove_all(stage, ec);
    throw;
  }
  return true;
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
