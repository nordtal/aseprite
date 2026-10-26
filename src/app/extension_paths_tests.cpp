// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "tests/app_test.h"

#include "app/extensions.h"

using namespace app;

// An extension that is installed lands in a folder of its own inside
// the user's extensions folder, nowhere else

TEST(ExtensionPaths, ANameIsOneFolder)
{
  EXPECT_TRUE(is_valid_extension_name("nordtal-tools"));
  EXPECT_TRUE(is_valid_extension_name("my.extension"));
  EXPECT_FALSE(is_valid_extension_name(""));
  EXPECT_FALSE(is_valid_extension_name("."));
  EXPECT_FALSE(is_valid_extension_name(".."));
  EXPECT_FALSE(is_valid_extension_name("../data/extensions/x"));
  EXPECT_FALSE(is_valid_extension_name("a/b"));
  EXPECT_FALSE(is_valid_extension_name("a\\b"));
  EXPECT_FALSE(is_valid_extension_name("a..b"));
  EXPECT_FALSE(is_valid_extension_name("C:x"));
}

TEST(ExtensionPaths, AnArchiveEntryStaysInside)
{
  EXPECT_TRUE(is_safe_archive_entry("package.json"));
  EXPECT_TRUE(is_safe_archive_entry("scripts/a.lua"));
  EXPECT_TRUE(is_safe_archive_entry("scripts\\a.lua"));
  EXPECT_TRUE(is_safe_archive_entry("a..b/c"));
  EXPECT_FALSE(is_safe_archive_entry(""));
  EXPECT_FALSE(is_safe_archive_entry("../x.lua"));
  EXPECT_FALSE(is_safe_archive_entry("a/../../x.lua"));
  EXPECT_FALSE(is_safe_archive_entry("a\\..\\x.lua"));
  EXPECT_FALSE(is_safe_archive_entry("a/.."));
  EXPECT_FALSE(is_safe_archive_entry("/etc/x"));
  EXPECT_FALSE(is_safe_archive_entry("\\x"));
  EXPECT_FALSE(is_safe_archive_entry("C:/x"));
}
