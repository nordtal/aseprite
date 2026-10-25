// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "tests/app_test.h"

#include "app/settings_import.h"
#include "base/file_content.h"
#include "base/fs.h"

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

  import_aseprite_settings(from, to);

  EXPECT_EQ("[general]\nscreen_scale = 2\n", read(to, "aseprite.ini"));
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

  import_aseprite_settings(from, to);

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
