// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/ui/side_panel.h"

#include "app/app.h"
#include "app/ini_file.h"
#include "app/ui/main_window.h"
#include "ui/scale.h"

namespace app {

static constexpr auto kWidthSection = "layout:side_panel";

SidePanel::SidePanel()
{
  setExpansive(true);
  initTheme();
}

SidePanel::~SidePanel()
{
  // A panel that goes while the app ends can still be docked
  if (parent())
    App::instance()->mainWindow()->removeSidePanel(this);
}

void SidePanel::onInitTheme(ui::InitThemeEvent& ev)
{
  ui::VBox::onInitTheme(ev);
  // The same room to the window's edge as a dialog's content
  setBorder(gfx::Border(4 * ui::guiscale()));
}

int SidePanel::width() const
{
  if (!id().empty()) {
    if (int width = get_config_int(kWidthSection, id().c_str(), 0); width > 0)
      return width;
  }
  return m_width;
}

void SidePanel::setWidth(int width)
{
  m_width = width;
  if (!id().empty())
    set_config_int(kWidthSection, id().c_str(), width);
}

} // namespace app
