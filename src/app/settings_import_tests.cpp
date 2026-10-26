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
#include <cstdio>
#include <filesystem>

using namespace app;

namespace {

// An empty directory in the temp folder for one test
std::string fresh_dir(const std::string& name)
{
  const std::string dir = base::join_path(base::get_temp_path(), "settings_import_tests_" + name);
  std::filesystem::remove_all(std::filesystem::u8path(dir));
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

TEST(SettingsImport, AFailedCopyLeavesNoScriptAccessBehind)
{
  const std::string from = fresh_dir("from");
  const std::string to = fresh_dir("to");
  write(from, "aseprite.ini", "[general]\nscreen_scale = 2\n[script_access]\n0123 = 1\n");
  write(from, "palettes/a.gpl", "GIMP Palette\n");

  // A palette that can't be read makes the copy fail after aseprite.ini
  const std::string locked = base::join_path(from, "palettes/a.gpl");
  std::filesystem::permissions(locked, std::filesystem::perms::none);
  if (std::FILE* f = std::fopen(locked.c_str(), "rb")) {
    // Root reads it anyway
    std::fclose(f);
    std::filesystem::permissions(locked, std::filesystem::perms::owner_all);
    GTEST_SKIP();
  }

  EXPECT_ANY_THROW(import_aseprite_settings(from, to));
  std::filesystem::permissions(locked, std::filesystem::perms::owner_all);

  // Nothing landed, so nothing of script_access either
  EXPECT_TRUE(base::list_files(to).empty());
}

TEST(SettingsImport, RefusesATargetInsideTheSource)
{
  const std::string from = fresh_dir("outer");
  write(from, "aseprite.ini", "[general]\nscreen_scale = 2\n");
  write(from, "palettes/a.gpl", "GIMP Palette\n");
  const std::string to = base::join_path(from, "palettes/inner");

  EXPECT_FALSE(import_aseprite_settings(from, to));
  EXPECT_FALSE(base::is_directory(to));
}

TEST(SettingsImport, LeavesDirectorySymlinksBehind)
{
  const std::string from = fresh_dir("from");
  const std::string to = fresh_dir("to");
  write(from, "palettes/a.gpl", "GIMP Palette\n");
  std::error_code ec;
  std::filesystem::create_directory_symlink(base::join_path(from, "palettes"),
                                            base::join_path(from, "palettes/loop"),
                                            ec);
  if (ec)
    GTEST_SKIP();

  EXPECT_TRUE(import_aseprite_settings(from, to));
  EXPECT_EQ("GIMP Palette\n", read(to, "palettes/a.gpl"));
  EXPECT_FALSE(std::filesystem::exists(std::filesystem::symlink_status(
    std::filesystem::u8path(base::join_path(to, "palettes/loop")))));
}

TEST(SettingsImport, FindsOnlyAnotherDirectoryWithSettings)
{
  const std::string from = fresh_dir("found");
  const std::string to = fresh_dir("to");
  EXPECT_EQ("", find_aseprite_settings(from, to));
  EXPECT_EQ("", find_aseprite_settings("", to));

  write(from, "aseprite.ini", "[general]\n");
  EXPECT_EQ(from, find_aseprite_settings(from, to));
  EXPECT_EQ("", find_aseprite_settings(from, from));
  EXPECT_EQ("", find_aseprite_settings(from, base::join_path(from, "inner")));
}

TEST(SettingsImport, DropsScriptAccessFromAReadOnlyIni)
{
  const std::string from = fresh_dir("from");
  const std::string to = fresh_dir("to");
  write(from, "aseprite.ini", "[general]\nscreen_scale = 2\n[script_access]\n0123 = 1\n");
  const auto ini = std::filesystem::u8path(base::join_path(from, "aseprite.ini"));
  std::filesystem::permissions(ini,
                               std::filesystem::perms::owner_read |
                                 std::filesystem::perms::group_read |
                                 std::filesystem::perms::others_read);

  EXPECT_TRUE(import_aseprite_settings(from, to));
  std::filesystem::permissions(ini, std::filesystem::perms::owner_all);

  cfg::CfgFile copy;
  copy.load(base::join_path(to, "aseprite.ini"));
  std::vector<std::string> sections;
  copy.getAllSections(sections);
  EXPECT_EQ((std::vector<std::string>{ "general" }), sections);
}

TEST(SettingsImport, RefusesATargetInsideTheSourceUnderAnotherName)
{
  namespace fs = std::filesystem;
  const std::string from = fresh_dir("outer");
  write(from, "palettes/a.gpl", "GIMP Palette\n");

  // Other names of the same directory: a file system that ignores case
  // takes it in other cases, and macOS reaches its data volume through
  // /System/Volumes/Data too
  const fs::path real = fs::canonical(fs::u8path(from));
  const fs::path aliases[] = { real.parent_path() / "settings_import_tests_OUTER",
                               fs::path("/System/Volumes/Data") / real.relative_path() };
  int tried = 0;
  for (const fs::path& alias : aliases) {
    std::error_code ec;
    if (!fs::equivalent(real, alias, ec))
      continue;
    ++tried;
    const fs::path to = alias / "palettes" / "inner";
    EXPECT_FALSE(import_aseprite_settings(from, to.u8string())) << to;
    EXPECT_FALSE(fs::exists(to)) << to;
  }
  if (!tried)
    GTEST_SKIP();
}

TEST(SettingsImport, MergesPalettesAndKeepsTheOnesThereAre)
{
  const std::string from = fresh_dir("from");
  const std::string to = fresh_dir("to");
  write(from, "palettes/a.gpl", "theirs\n");
  write(from, "palettes/b.gpl", "theirs\n");
  write(from, "palettes/more/d.gpl", "theirs\n");
  write(from, "palettes/more/e.gpl", "theirs\n");
  write(to, "palettes/a.gpl", "mine\n");
  write(to, "palettes/c.gpl", "mine\n");
  write(to, "palettes/more/d.gpl", "mine\n");

  EXPECT_TRUE(import_aseprite_settings(from, to));

  EXPECT_EQ("mine\n", read(to, "palettes/a.gpl"));
  EXPECT_EQ("theirs\n", read(to, "palettes/b.gpl"));
  EXPECT_EQ("mine\n", read(to, "palettes/c.gpl"));
  EXPECT_EQ("mine\n", read(to, "palettes/more/d.gpl"));
  EXPECT_EQ("theirs\n", read(to, "palettes/more/e.gpl"));
}
