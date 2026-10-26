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
#include "ui/splitter.h"

#include <map>

namespace app {

static constexpr auto kWidthSection = "layout:side_panel";
static constexpr auto kSplitSection = "layout:side_panel_split";

// The values of the panels that keep theirs for the session only, by
// section and key
static std::map<std::pair<std::string, std::string>, double> g_session;

// A splitter that the panel keeps the position of once the user drags
// it
class SidePanel::Splitter : public ui::Splitter {
public:
  explicit Splitter(SidePanel* panel) : ui::Splitter(ByPercentage, ui::VERTICAL), m_panel(panel)
  {
    setExpansive(true);
    initTheme();
  }

private:
  // The panel's face, and the bar between the parts in the colour of
  // the dock's splitter beside the panel
  void onPaint(ui::PaintEvent& ev) override
  {
    auto* g = ev.graphics();
    auto* theme = skin::SkinTheme::get(this);
    g->fillRect(theme->colors.windowFace(), clientBounds());
    if (children().size() == 2) {
      const int y = children()[0]->bounds().y2() - bounds().y;
      g->fillRect(theme->colors.workspace(),
                  gfx::Rect(0, y, bounds().w, children()[1]->bounds().y - children()[0]->bounds().y2()));
    }
  }

  void onPositionChange() override
  {
    ui::Splitter::onPositionChange();
    if (hasCapture())
      m_panel->keep(kSplitSection, getPosition());
  }

  SidePanel* m_panel;
};

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

double SidePanel::kept(const char* section) const
{
  if (m_widthKept)
    return get_config_double(section, m_widthKey.c_str(), 0);
  auto it = g_session.find({ section, m_widthKey });
  return (it != g_session.end() ? it->second : 0);
}

void SidePanel::keep(const char* section, double value)
{
  if (m_widthKept)
    set_config_double(section, m_widthKey.c_str(), value);
  else
    g_session[{ section, m_widthKey }] = value;
}

int SidePanel::width() const
{
  const int width = int(kept(kWidthSection));
  return (width > 0 ? width : m_width);
}

void SidePanel::setWidth(int width)
{
  m_width = width;
  keep(kWidthSection, width);
}

void SidePanel::split(ui::Widget* above, ui::Widget* below)
{
  auto splitter = new Splitter(this);
  const double position = kept(kSplitSection);
  splitter->setPosition(position > 0 ? position : 50);
  replaceChild(above, splitter);
  splitter->addChild(above);
  splitter->addChild(below);
}

} // namespace app
