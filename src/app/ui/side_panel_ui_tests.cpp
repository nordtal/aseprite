// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#define TEST_GUI
#include "tests/app_test.h"

#include "app/ini_file.h"
#include "app/ui/side_panel.h"
#include "ui/message.h"

using namespace app;

namespace {

// Settings in memory for one test, never in the user's config
struct Config {
  Config() { push_config_state(); }
  ~Config() { pop_config_state(); }
};

// A splitter 100 px high with a part above and one below
struct Panel {
  SidePanelSplitter splitter;

  explicit Panel(SidePanelKept* kept) : splitter(kept)
  {
    splitter.addChild(new ui::Widget);
    splitter.addChild(new ui::Widget);
    splitter.setBounds(gfx::Rect(0, 0, 10, 100));
  }

  // Moves the mouse to y while the splitter has the mouse, as the
  // user does while dragging its bar
  void drag(int y)
  {
    splitter.enableFlags(ui::HAS_CAPTURE);
    ui::MouseMessage msg(ui::kMouseMoveMessage,
                         ui::PointerType::Mouse,
                         ui::kButtonLeft,
                         ui::kKeyNoneModifier,
                         gfx::Point(5, y));
    splitter.sendMessage(&msg);
    splitter.disableFlags(ui::HAS_CAPTURE);
  }
};

} // namespace

TEST(SidePanelSplitter, KeepsWhereTheUserDragsIt)
{
  Config config;
  SidePanelKept kept("drag", true);
  Panel panel(&kept);

  panel.drag(25);
  EXPECT_DOUBLE_EQ(25, SidePanelKept("drag", true).split());
}

TEST(SidePanelSplitter, KeepsNothingThatAScriptOrTheLayoutDoes)
{
  Config config;
  SidePanelKept kept("script", true);
  Panel panel(&kept);

  panel.splitter.setPosition(30);
  panel.splitter.layout();
  EXPECT_DOUBLE_EQ(-1, kept.split());
}

TEST(SidePanelSplitter, ADragPastAnEdgeKeepsTheEdge)
{
  Config config;
  SidePanelKept kept("edge", true);
  Panel panel(&kept);

  panel.drag(-30);
  EXPECT_DOUBLE_EQ(0, SidePanelKept("edge", true).split());

  panel.drag(130);
  EXPECT_DOUBLE_EQ(100, SidePanelKept("edge", true).split());
}

TEST(SidePanelSplitter, APanelWithoutIdKeepsItsPlaceForTheSession)
{
  Config config;
  SidePanelKept kept("owner\nTitle", false);
  Panel panel(&kept);

  panel.drag(-30);
  kept.setWidth(180);
  EXPECT_DOUBLE_EQ(0, SidePanelKept("owner\nTitle", false).split());
  EXPECT_EQ(180, SidePanelKept("owner\nTitle", false).width());
  EXPECT_EQ(nullptr, get_config_string("layout:side_panel_split", "owner\nTitle", nullptr));
  EXPECT_EQ(nullptr, get_config_string("layout:side_panel", "owner\nTitle", nullptr));
}

TEST(SidePanelKept, KeepsTheWidthAsAWholeNumber)
{
  Config config;
  SidePanelKept("width", true).setWidth(220);
  EXPECT_STREQ("220", get_config_string("layout:side_panel", "width", ""));

  // A width from before the splitter
  set_config_string("layout:side_panel", "earlier", "180");
  EXPECT_EQ(180, SidePanelKept("earlier", true).width());
  EXPECT_EQ(0, SidePanelKept("none", true).width());
}
