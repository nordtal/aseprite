// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_UI_SIDE_PANEL_H_INCLUDED
#define APP_UI_SIDE_PANEL_H_INCLUDED
#pragma once

#include "app/ui/dockable.h"
#include "ui/box.h"
#include "ui/splitter.h"

#include <string>

namespace app {

// The width and the splitter's place that a side panel keeps under
// its key: in the user's config across restarts if it's kept,
// otherwise for the session
class SidePanelKept {
public:
  SidePanelKept(const std::string& key = std::string(), bool inConfig = false)
    : m_key(key)
    , m_inConfig(inConfig)
  {
  }

  // The width in points, or 0 if there's none
  int width() const;
  void setWidth(int width);

  // The splitter's place in percent of the panel's height, or -1 if
  // there's none
  double split() const;
  // Keeps the place within 0..100
  void setSplit(double position);

private:
  std::string m_key;
  bool m_inConfig;
};

// The splitter of a side panel (see SidePanel::split()). Where the
// user drags it goes to kept, a position that a script or the layout
// gives it doesn't.
class SidePanelSplitter : public ui::Splitter {
public:
  explicit SidePanelSplitter(SidePanelKept* kept);

protected:
  void onPaint(ui::PaintEvent& ev) override;
  void onPositionChange() override;

private:
  SidePanelKept* m_kept;
};

// A column of widgets that a workspace view gives the main window
// (see WorkspaceView::sidePanel()). The main window docks it at its
// far edge, outside the colour bar, while the view is active.
class SidePanel : public ui::VBox,
                  public Dockable {
public:
  SidePanel();
  ~SidePanel();

  // ui::LEFT is the side of the colour bar, ui::RIGHT the other one
  int side() const { return m_side; }
  void setSide(int side) { m_side = side; }

  // The width in points. A panel keeps the width that the user or the
  // script set under its key, in the user's config across restarts if
  // it's kept, otherwise for the session, and uses the default width
  // until it has one.
  int width() const;
  void setWidth(int width);
  void setDefaultWidth(int width) { m_width = width; }
  void setWidthKey(const std::string& key, bool kept);

  // Moves the widget above, which the panel holds, and the widget
  // below into a splitter that the user drags to share the panel's
  // height between them. It starts at half, and the panel keeps where
  // the user dragged it under its width key.
  void split(ui::Widget* above, ui::Widget* below);

  // Dockable implementation
  int dockableAt() const override { return ui::LEFT | ui::RIGHT | ui::EXPANSIVE; }
  int dockHandleSide() const override { return 0; }

protected:
  void onInitTheme(ui::InitThemeEvent& ev) override;
  void onPaint(ui::PaintEvent& ev) override;

private:
  int m_side = ui::LEFT;
  int m_width = 0;
  SidePanelKept m_kept;
};

} // namespace app

#endif
