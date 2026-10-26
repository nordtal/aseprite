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

namespace app {

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

  // The width in points. A panel with an id keeps the width that the
  // user or the script set across restarts, and uses the default width
  // until it has one.
  int width() const;
  void setWidth(int width);
  void setDefaultWidth(int width) { m_width = width; }

  // Dockable implementation
  int dockableAt() const override { return ui::LEFT | ui::RIGHT | ui::EXPANSIVE; }
  int dockHandleSide() const override { return 0; }

protected:
  void onInitTheme(ui::InitThemeEvent& ev) override;

private:
  int m_side = ui::LEFT;
  int m_width = 0;
};

} // namespace app

#endif
