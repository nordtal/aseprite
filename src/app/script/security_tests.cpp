// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "tests/app_test.h"

#include "app/script/security.h"

using namespace app::script;

namespace {

// Each byte is one point wide
int bytes(const std::string& s)
{
  return int(s.size());
}

} // anonymous namespace

TEST(AlertLineWidth, TheWidestTextShrinksByTheExcess)
{
  EXPECT_EQ(300, alert_line_width(500, 200));
}

TEST(AlertLineWidth, ALineIsNeverNarrowerThan160Points)
{
  EXPECT_EQ(160, alert_line_width(500, 499));
}

TEST(BreakLines, ATextThatFitsStaysOneLine)
{
  EXPECT_EQ(std::vector<std::string>{ "/a/b.lua" }, break_lines("/a/b.lua", 8, bytes));
}

TEST(BreakLines, APathBreaksAfterTheLastSlashOrSpaceThatFits)
{
  EXPECT_EQ((std::vector<std::string>{ "/Users/", "Application ", "Support/", "tab.lua" }),
            break_lines("/Users/Application Support/tab.lua", 12, bytes));
}

TEST(BreakLines, APartWithoutABreakBreaksAtTheWidth)
{
  EXPECT_EQ((std::vector<std::string>{ "127.0.0.1:", "29415" }),
            break_lines("127.0.0.1:29415", 10, bytes));
}

TEST(BreakLines, ACharacterIsNeverSplit)
{
  // "\303\274" is one character of two bytes: "ab" and its first byte
  // would fit
  EXPECT_EQ((std::vector<std::string>{ "ab", "\303\274c" }), break_lines("ab\303\274c", 3, bytes));
}

TEST(BreakLines, ACharacterWiderThanTheWidthIsALineOfItsOwn)
{
  EXPECT_EQ((std::vector<std::string>{ "a", "b" }), break_lines("ab", 0, bytes));
}
