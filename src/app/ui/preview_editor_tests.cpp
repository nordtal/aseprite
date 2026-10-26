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

TEST(PreviewPlaceChanged, AWindowThatStayedWhereItOpenedHasNotChanged)
{
  const gfx::Rect bounds(200, 100, 160, 120);
  EXPECT_FALSE(preview_place_changed(bounds, gfx::Rect(), bounds, gfx::Rect()));

  // In the native mode the bounds are on the window's own display
  const gfx::Rect local(0, 0, 160, 120), frame(900, 500, 320, 240);
  EXPECT_FALSE(preview_place_changed(local, frame, local, frame));
}

TEST(PreviewPlaceChanged, AMovedOrResizedWindowHasChanged)
{
  const gfx::Rect bounds(200, 100, 160, 120);
  EXPECT_TRUE(
    preview_place_changed(bounds, gfx::Rect(), gfx::Rect(240, 100, 160, 120), gfx::Rect()));
  EXPECT_TRUE(
    preview_place_changed(bounds, gfx::Rect(), gfx::Rect(200, 100, 200, 120), gfx::Rect()));
}

TEST(PreviewPlaceChanged, AMovedOrResizedNativeWindowHasChanged)
{
  const gfx::Rect local(0, 0, 160, 120), frame(900, 500, 320, 240);

  // A move keeps the bounds on the window's own display
  EXPECT_TRUE(preview_place_changed(local, frame, local, gfx::Rect(700, 500, 320, 240)));

  // A resize from the bottom right corner keeps the frame's origin
  EXPECT_TRUE(
    preview_place_changed(local, frame, gfx::Rect(0, 0, 200, 150), gfx::Rect(900, 500, 400, 300)));
}
