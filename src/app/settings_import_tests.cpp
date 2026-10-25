// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "tests/app_test.h"

#include "app/settings_import.h"
#include "base/file_content.h"
#include "base/fs.h"
#include "cfg/cfg.h"

#include <algorithm>
#include <filesystem>

using namespace app;

namespace {

void remove_tree(const std::string& path)
{
  if (base::is_directory(path)) {
    for (const auto& item : base::list_files(path))
      remove_tree(base::join_path(path, item));
    base::remove_directory(path);
  }
  else if (base::is_file(path))
    base::delete_file(path);
}

// An empty directory in the temp folder for one test
std::string fresh_dir(const std::string& name)
{
  const std::string dir = base::join_path(base::get_temp_path(), "settings_import_tests_" + name);
  remove_tree(dir);
  base::make_all_directories(dir);
  return dir;
}

void write(const std::string& dir, const std::string& name, const std::string& text)
{
  const std::string fn = base::join_path(dir, name);
  base::make_all_directories(base::get_file_path(fn));
  base::write_file_content(fn, (const uint8_t*)text.data(), text.size());
}

std::string read(const std::string& dir, const std::string& name)
{
  const auto buf = base::read_file_content(base::join_path(dir, name));
  return std::string(buf.begin(), buf.end());
}

} // anonymous namespace

TEST(SettingsImport, CopiesPreferencesPalettesAndShortcuts)
{
  const std::string from = fresh_dir("from");
  const std::string to = fresh_dir("to");
  write(from, "aseprite.ini", "[general]\nscreen_scale = 2\n");
  write(from, "user.aseprite-keys", "<keys />\n");
  write(from, "palettes/a.gpl", "GIMP Palette\n");
  write(from, "palettes/more/b.gpl", "GIMP Palette\n");
  write(from, "extensions/x/package.json", "{}\n");
  write(from, "Aseprite.log", "log\n");

  EXPECT_TRUE(import_aseprite_settings(from, to));

  cfg::CfgFile ini;
  ini.load(base::join_path(to, "aseprite.ini"));
  EXPECT_EQ(2, ini.getIntValue("general", "screen_scale", 0));
  EXPECT_EQ("<keys />\n", read(to, "user.aseprite-keys"));
  EXPECT_EQ("GIMP Palette\n", read(to, "palettes/a.gpl"));
  EXPECT_EQ("GIMP Palette\n", read(to, "palettes/more/b.gpl"));
  EXPECT_FALSE(base::is_directory(base::join_path(to, "extensions")));
  EXPECT_FALSE(base::is_file(base::join_path(to, "Aseprite.log")));
}

TEST(SettingsImport, CopiesWhatThereIs)
{
  const std::string from = fresh_dir("from");
  const std::string to = fresh_dir("to");
  write(from, "palettes/a.gpl", "GIMP Palette\n");

  EXPECT_TRUE(import_aseprite_settings(from, to));

  EXPECT_EQ("GIMP Palette\n", read(to, "palettes/a.gpl"));
  EXPECT_FALSE(base::is_file(base::join_path(to, "aseprite.ini")));
}

TEST(SettingsImport, OnlyADirectoryWithSettingsHasThem)
{
  const std::string dir = fresh_dir("dir");
  EXPECT_FALSE(has_aseprite_settings(base::join_path(dir, "missing")));
  write(dir, "extensions/x/package.json", "{}\n");
  EXPECT_FALSE(has_aseprite_settings(dir));
  write(dir, "user.aseprite-keys", "<keys />\n");
  EXPECT_TRUE(has_aseprite_settings(dir));
}

TEST(SettingsImport, DropsRecentFilesAndScriptAccess)
{
  const std::string from = fresh_dir("from");
  const std::string to = fresh_dir("to");
  write(from,
        "aseprite.ini",
        "[general]\nscreen_scale = 2\n"
        "[RecentFiles]\n0000 = /a.aseprite\n"
        "[RecentPaths]\n0000 = /art\n"
        "[PinnedFiles]\n0000 = /b.aseprite\n"
        "[script_access]\n0123456789abcdef = 1\n"
        "[theme]\nselected = dark\n");

  EXPECT_TRUE(import_aseprite_settings(from, to));

  cfg::CfgFile ini;
  ini.load(base::join_path(to, "aseprite.ini"));
  std::vector<std::string> sections;
  ini.getAllSections(sections);
  std::sort(sections.begin(), sections.end());
  EXPECT_EQ((std::vector<std::string>{ "PinnedFiles", "general", "theme" }), sections);
  EXPECT_EQ(2, ini.getIntValue("general", "screen_scale", 0));
  EXPECT_STREQ("/b.aseprite", ini.getValue("PinnedFiles", "0000", ""));
  EXPECT_STREQ("dark", ini.getValue("theme", "selected", ""));

  // The source keeps everything
  EXPECT_NE(std::string::npos, read(from, "aseprite.ini").find("[script_access]"));
}

TEST(SettingsImport, RefusesToCopyADirectoryOntoItself)
{
  const std::string from = fresh_dir("self");
  write(from, "aseprite.ini", "[general]\nscreen_scale = 2\n");
  write(from, "palettes/a.gpl", "GIMP Palette\n");

  // The same directory, also through a symlink
  const std::string link = base::join_path(base::get_temp_path(), "settings_import_tests_link");
  std::error_code ec;
  std::filesystem::remove(link, ec);
  std::filesystem::create_directory_symlink(from, link, ec);

  EXPECT_FALSE(import_aseprite_settings(from, from));
  if (!ec)
    EXPECT_FALSE(import_aseprite_settings(from, link));
  EXPECT_EQ("[general]\nscreen_scale = 2\n", read(from, "aseprite.ini"));
  EXPECT_EQ("GIMP Palette\n", read(from, "palettes/a.gpl"));
  std::filesystem::remove(link, ec);
}
