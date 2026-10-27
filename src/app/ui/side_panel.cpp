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

#include <algorithm>
#include <map>

namespace app {

static constexpr auto kWidthSection = "layout:side_panel";
static constexpr auto kSplitSection = "layout:side_panel_split";

// The values of the panels that keep theirs for the session only, by
// section and key
static std::map<std::pair<std::string, std::string>, double> g_session;

int SidePanelKept::width() const
{
  if (m_inConfig)
    return get_config_int(kWidthSection, m_key.c_str(), 0);
  auto it = g_session.find({ kWidthSection, m_key });
  return (it != g_session.end() ? int(it->second) : 0);
}

void SidePanelKept::setWidth(int width)
{
  if (m_inConfig)
    set_config_int(kWidthSection, m_key.c_str(), width);
  else
    g_session[{ kWidthSection, m_key }] = width;
}

double SidePanelKept::split() const
{
  if (m_inConfig)
    return get_config_double(kSplitSection, m_key.c_str(), -1);
  auto it = g_session.find({ kSplitSection, m_key });
  return (it != g_session.end() ? it->second : -1);
}

void SidePanelKept::setSplit(double position)
{
  // A drag goes on past the panel's edges
  position = std::clamp(position, 0.0, 100.0);
  if (m_inConfig)
    set_config_double(kSplitSection, m_key.c_str(), position);
  else
    g_session[{ kSplitSection, m_key }] = position;
}

SidePanelSplitter::SidePanelSplitter(SidePanelKept* kept)
  : ui::Splitter(ByPercentage, ui::VERTICAL)
  , m_kept(kept)
{
  setExpansive(true);
}

// The panel's face, and the bar between the parts in the colour of
// the dock's splitter beside the panel
void SidePanelSplitter::onPaint(ui::PaintEvent& ev)
{
  auto* g = ev.graphics();
  auto* theme = skin::SkinTheme::get(this);
  g->fillRect(theme->colors.windowFace(), clientBounds());
  const ui::Widget* above = panel1();
  const ui::Widget* below = panel2();
  if (above && below) {
    const int y = above->bounds().y2() - bounds().y;
    g->fillRect(theme->colors.workspace(),
                gfx::Rect(0, y, bounds().w, below->bounds().y - above->bounds().y2()));
  }
}

void SidePanelSplitter::onPositionChange()
{
  ui::Splitter::onPositionChange();
  if (hasCapture())
    m_kept->setSplit(getPosition());
}

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
  m_kept = SidePanelKept(key, kept);
}

int SidePanel::width() const
{
  const int width = m_kept.width();
  return (width > 0 ? width : m_width);
}

void SidePanel::setWidth(int width)
{
  m_width = width;
  m_kept.setWidth(width);
}

void SidePanel::split(ui::Widget* above, ui::Widget* below)
{
  auto splitter = new SidePanelSplitter(&m_kept);
  const double position = m_kept.split();
  splitter->setPosition(position >= 0 ? position : 50);
  replaceChild(above, splitter);
  splitter->addChild(above);
  splitter->addChild(below);
}

} // namespace app
