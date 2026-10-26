// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "tests/app_test.h"

#include "app/ui/preview_editor.h"

using namespace app;

TEST(DefaultPreviewBounds, TheBottomRightCornerOfTheEditorsViewInsetByTheGap)
{
  // An editor between a side panel and the colour bar, in either order
  EXPECT_EQ(gfx::Rect(300 - 4 - 100, 500 - 4 - 80, 100, 80),
            default_preview_bounds(gfx::Rect(0, 0, 300, 500), gfx::Size(100, 80), 4));
  EXPECT_EQ(gfx::Rect(240 + 260 - 4 - 100, 30 + 400 - 4 - 80, 100, 80),
            default_preview_bounds(gfx::Rect(240, 30, 260, 400), gfx::Size(100, 80), 4));
}

TEST(DefaultPreviewBounds, AViewSmallerThanThePreviewKeepsItsTopLeftCorner)
{
  EXPECT_EQ(gfx::Rect(240, 30, 100, 80),
            default_preview_bounds(gfx::Rect(240, 30, 60, 50), gfx::Size(100, 80), 4));
}
