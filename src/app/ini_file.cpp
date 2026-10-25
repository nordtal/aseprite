// Aseprite
// Copyright (C) 2018-2024  Igara Studio S.A.
// Copyright (C) 2001-2016  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/ini_file.h"

#include "app/app.h"
#include "app/resource_finder.h"
#include "app/settings_import.h"
#include "base/fs.h"
#include "base/log.h"
#include "base/split_string.h"
#include "base/string.h"
#include "cfg/cfg.h"
#include "fmt/format.h"

#include <cstdlib>
#include <vector>

namespace app {

using namespace gfx;

static std::string g_configFilename;
static std::vector<cfg::CfgFile*> g_configs;

ConfigModule::ConfigModule()
{
  ResourceFinder rf;
  rf.includeUserDir("aseprite.ini");

  // getFirstOrCreateDefault() will create the app's directory
  // inside the OS configuration folder.
  std::string fn = rf.getFirstOrCreateDefault();

  // On the first start, the settings of an installed Aseprite can
  // be the start of ours. Aseprite's own migrations of its older
  // config locations would move its files here, so there are none.
  if (!base::is_file(fn) && App::instance() && App::instance()->isGui()) {
    try {
      const std::string to = base::get_file_path(fn);
      const std::string from = find_aseprite_settings(to);
      if (!from.empty() && ask_to_import_aseprite_settings(from))
        import_aseprite_settings(from, to);
    }
    catch (const std::exception& ex) {
      LOG(ERROR, "Copying the settings of Aseprite failed: %s\n", ex.what());
    }
  }

  set_config_file(fn.c_str());
  g_configFilename = fn;
}

ConfigModule::~ConfigModule()
{
  // Don't flush config files here, preferences are saved from ~App()
  // directly.

  // Delete all config files.
  for (auto cfg : g_configs)
    delete cfg;
  g_configs.clear();
}

//////////////////////////////////////////////////////////////////////
// Allegro-like API to handle .ini configuration files

void push_config_state()
{
  g_configs.push_back(new cfg::CfgFile());
}

void pop_config_state()
{
  ASSERT(!g_configs.empty());

  delete g_configs.back();
  g_configs.erase(--g_configs.end());
}

void flush_config_file()
{
  ASSERT(!g_configs.empty());

  g_configs.back()->save();
}

void set_config_file(const char* filename)
{
  if (g_configs.empty())
    g_configs.push_back(new cfg::CfgFile());

  g_configs.back()->load(filename);
}

std::string main_config_filename()
{
  return g_configFilename;
}

const char* get_config_string(const char* section, const char* name, const char* value)
{
  return g_configs.back()->getValue(section, name, value);
}

void set_config_string(const char* section, const char* name, const char* value)
{
  g_configs.back()->setValue(section, name, value);
}

int get_config_int(const char* section, const char* name, int value)
{
  return g_configs.back()->getIntValue(section, name, value);
}

void set_config_int(const char* section, const char* name, int value)
{
  g_configs.back()->setIntValue(section, name, value);
}

float get_config_float(const char* section, const char* name, float value)
{
  return (float)g_configs.back()->getDoubleValue(section, name, (float)value);
}

void set_config_float(const char* section, const char* name, float value)
{
  g_configs.back()->setDoubleValue(section, name, (float)value);
}

double get_config_double(const char* section, const char* name, double value)
{
  return g_configs.back()->getDoubleValue(section, name, value);
}

void set_config_double(const char* section, const char* name, double value)
{
  g_configs.back()->setDoubleValue(section, name, value);
}

bool get_config_bool(const char* section, const char* name, bool value)
{
  return g_configs.back()->getBoolValue(section, name, value);
}

void set_config_bool(const char* section, const char* name, bool value)
{
  g_configs.back()->setBoolValue(section, name, value);
}

Point get_config_point(const char* section, const char* name, const Point& point)
{
  Point point2(point);
  const char* value = get_config_string(section, name, "");
  if (value) {
    std::vector<std::string> parts;
    base::split_string(value, parts, " ");
    if (parts.size() == 2) {
      point2.x = strtol(parts[0].c_str(), NULL, 10);
      point2.y = strtol(parts[1].c_str(), NULL, 10);
    }
  }
  return point2;
}

void set_config_point(const char* section, const char* name, const Point& point)
{
  std::string buf = fmt::format("{} {}", point.x, point.y);
  set_config_string(section, name, buf.c_str());
}

Size get_config_size(const char* section, const char* name, const Size& size)
{
  Size size2(size);
  const char* value = get_config_string(section, name, "");
  if (value) {
    std::vector<std::string> parts;
    base::split_string(value, parts, " ");
    if (parts.size() == 2) {
      size2.w = strtol(parts[0].c_str(), NULL, 10);
      size2.h = strtol(parts[1].c_str(), NULL, 10);
    }
  }
  return size2;
}

void set_config_size(const char* section, const char* name, const Size& size)
{
  std::string buf = fmt::format("{} {}", size.w, size.h);
  set_config_string(section, name, buf.c_str());
}

Rect get_config_rect(const char* section, const char* name, const Rect& rect)
{
  Rect rect2(rect);
  const char* value = get_config_string(section, name, "");
  if (value) {
    std::vector<std::string> parts;
    base::split_string(value, parts, " ");
    if (parts.size() == 4) {
      rect2.x = strtol(parts[0].c_str(), NULL, 10);
      rect2.y = strtol(parts[1].c_str(), NULL, 10);
      rect2.w = strtol(parts[2].c_str(), NULL, 10);
      rect2.h = strtol(parts[3].c_str(), NULL, 10);
    }
  }
  return rect2;
}

void set_config_rect(const char* section, const char* name, const Rect& rect)
{
  std::string buf = fmt::format("{} {} {} {}", rect.x, rect.y, rect.w, rect.h);
  set_config_string(section, name, buf.c_str());
}

app::Color get_config_color(const char* section, const char* name, const app::Color& value)
{
  return app::Color::fromString(get_config_string(section, name, value.toString().c_str()));
}

void set_config_color(const char* section, const char* name, const app::Color& value)
{
  set_config_string(section, name, value.toString().c_str());
}

void del_config_value(const char* section, const char* name)
{
  g_configs.back()->deleteValue(section, name);
}

void del_config_section(const char* section)
{
  g_configs.back()->deleteSection(section);
}

base::paths enum_config_keys(const char* section)
{
  base::paths keys;
  g_configs.back()->getAllKeys(section, keys);
  return keys;
}

} // namespace app
