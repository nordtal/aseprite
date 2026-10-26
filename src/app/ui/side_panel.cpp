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
#include "app/ui/skin/skin_theme.h"
#include "ui/graphics.h"
#include "ui/paint_event.h"
#include "ui/scale.h"

#include <map>

namespace app {

static constexpr auto kWidthSection = "layout:side_panel";

// The widths of the panels that keep theirs for the session only
static std::map<std::string, int> g_sessionWidths;

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
  setBgColor(skin::SkinTheme::get(this)->colors.windowFace());
}

void SidePanel::onPaint(ui::PaintEvent& ev)
{
  // The dock behind it has the colour of the splitter
  ev.graphics()->fillRect(bgColor(), clientBounds());
}

void SidePanel::setWidthKey(const std::string& key, bool kept)
{
  m_widthKey = key;
  m_widthKept = kept;
}

int SidePanel::width() const
{
  int width = 0;
  if (m_widthKept)
    width = get_config_int(kWidthSection, m_widthKey.c_str(), 0);
  else if (auto it = g_sessionWidths.find(m_widthKey); it != g_sessionWidths.end())
    width = it->second;
  return (width > 0 ? width : m_width);
}

void SidePanel::setWidth(int width)
{
  m_width = width;
  if (m_widthKept)
    set_config_int(kWidthSection, m_widthKey.c_str(), width);
  else
    g_sessionWidths[m_widthKey] = width;
}

} // namespace app
