// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "tests/app_test.h"

#include "app/resource_finder.h"

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

using namespace app;

namespace {

void set_env(const char* name, const std::string& value)
{
#ifdef _WIN32
  _putenv_s(name, value.c_str());
#else
  setenv(name, value.c_str(), 1);
#endif
}

std::vector<std::string> paths(ResourceFinder& rf)
{
  std::vector<std::string> result;
  while (rf.next())
    result.push_back(rf.filename());
  return result;
}

bool inside(const std::string& path, const std::string& dir)
{
  return path.compare(0, dir.size(), dir) == 0;
}

} // anonymous namespace

// The extensions in the data folder of the app are the ones that
// come with it. A data folder in the user's folder is the user's.
TEST(ResourceFinder, TheAppDataFolderLeavesTheUserFolderOut)
{
  const std::string user = (std::filesystem::temp_directory_path() / "resource_finder_tests").string();
  set_env("ASEPRITE_USER_FOLDER", user);
  set_env("HOME", user);
  set_env("XDG_CONFIG_HOME", user);
  set_env("AppData", user);

  ResourceFinder all(false);
  all.includeDataDir("extensions");
  bool userData = false;
  for (const auto& path : paths(all))
    userData |= inside(path, user);
  ASSERT_TRUE(userData);

  ResourceFinder app(false);
  app.includeAppDataDir("extensions");
  const auto appPaths = paths(app);
  EXPECT_FALSE(appPaths.empty());
  for (const auto& path : appPaths)
    EXPECT_FALSE(inside(path, user)) << path;
}
