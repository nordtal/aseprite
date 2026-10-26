// Aseprite
// Copyright (C) 2019-2025  Igara Studio S.A.
// Copyright (C) 2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/script/security.h"

#include "app/app.h"
#include "app/context.h"
#include "app/i18n/strings.h"
#include "app/ini_file.h"
#include "app/launcher.h"
#include "app/script/engine.h"
#include "app/script/luacpp.h"
#include "app/script/trust.h"
#include "base/convert_to.h"
#include "base/fs.h"
#include "base/sha1.h"
#include "fmt/format.h"
#include "ui/link_label.h"
#include "ui/manager.h"
#include "ui/scale.h"
#include "ui/widget_type.h"

#include "script_access.xml.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <unordered_map>
#include <vector>

namespace app { namespace script {

namespace {

// The space the alert keeps from each side of the window, in points
const int kAlertMargin = 8;

int secure_io_open(lua_State* L);
int secure_io_popen(lua_State* L);
int secure_io_lines(lua_State* L);
int secure_io_input(lua_State* L);
int secure_io_output(lua_State* L);
int secure_os_execute(lua_State* L);
int secure_os_remove(lua_State* L);
int secure_os_rename(lua_State* L);
int secure_package_loadlib(lua_State* L);
int text_load(lua_State* L);
int text_loadfile(lua_State* L);

enum {
  io_open,
  io_popen,
  io_lines,
  io_input,
  io_output,
  os_execute,
  os_remove,
  os_rename,
  package_loadlib,
  base_load,
  base_loadfile,
};

static struct {
  const char* package;
  const char* funcname;
  lua_CFunction newfunc;
  lua_CFunction origfunc = nullptr;
} replaced_functions[] = {
  { "io",      "open",    secure_io_open         },
  { "io",      "popen",   secure_io_popen        },
  { "io",      "lines",   secure_io_lines        },
  { "io",      "input",   secure_io_input        },
  { "io",      "output",  secure_io_output       },
  { "os",      "execute", secure_os_execute      },
  { "os",      "remove",  secure_os_remove       },
  { "os",      "rename",  secure_os_rename       },
  { "package", "loadlib", secure_package_loadlib },
  { "_G",      "load",    text_load              },
  { "_G",      "loadfile", text_loadfile         },
};

// Map from .lua file name -> sha1
std::unordered_map<std::string, std::string> g_keys;

std::string get_key(const std::string& source)
{
  auto it = g_keys.find(source);
  if (it != g_keys.end())
    return it->second;
  else
    return g_keys[source] = base::convert_to<std::string>(base::Sha1::calculateFromString(source));
}

std::string get_script_filename(lua_State* L, int stackLevel)
{
  std::string script;
  lua_Debug ar;
  if (!lua_getstack(L, stackLevel - 1, &ar))
    return script;

  if (!lua_getinfo(L, "S", &ar))
    return script;

  script = ar.source;
  if (!script.empty() && script[0] == '@')
    script = ar.source + 1;

  return script;
}

int unsupported(lua_State* L)
{
  // debug.getinfo(1, "n").name
  lua_getglobal(L, "debug");
  lua_getfield(L, -1, "getinfo");
  lua_remove(L, -2);
  lua_pushinteger(L, 1);
  lua_pushstring(L, "n");
  lua_call(L, 2, 1);
  lua_getfield(L, -1, "name");
  return luaL_error(L, "unsupported function '%s'", lua_tostring(L, -1));
}

int secure_io_open(lua_State* L)
{
  std::string absFilename = base::get_absolute_path(luaL_checkstring(L, 1));

  FileAccessMode mode = FileAccessMode::Read; // Read is the default access
  if (lua_tostring(L, 2) && std::strchr(lua_tostring(L, 2), 'w') != nullptr) {
    mode = FileAccessMode::Write;
  }

  if (!ask_access(L, absFilename.c_str(), mode, ResourceType::File)) {
    return luaL_error(L, "the script doesn't have access to file '%s'", absFilename.c_str());
  }
  return replaced_functions[io_open].origfunc(L);
}

int secure_io_popen(lua_State* L)
{
  const char* cmd = luaL_checkstring(L, 1);
  if (!ask_access(L, cmd, FileAccessMode::Execute, ResourceType::Command)) {
    // Stop script
    return luaL_error(L, "the script doesn't have access to execute the command: '%s'", cmd);
  }
  return replaced_functions[io_popen].origfunc(L);
}

int secure_io_lines(lua_State* L)
{
  if (auto fn = lua_tostring(L, 1)) {
    std::string absFilename = base::get_absolute_path(fn);

    if (!ask_access(L, absFilename.c_str(), FileAccessMode::Read, ResourceType::File)) {
      return luaL_error(L, "the script doesn't have access to file '%s'", absFilename.c_str());
    }
  }
  return replaced_functions[io_lines].origfunc(L);
}

int secure_io_input(lua_State* L)
{
  if (auto fn = lua_tostring(L, 1)) {
    std::string absFilename = base::get_absolute_path(fn);

    if (!ask_access(L, absFilename.c_str(), FileAccessMode::Read, ResourceType::File)) {
      return luaL_error(L, "the script doesn't have access to file '%s'", absFilename.c_str());
    }
  }
  return replaced_functions[io_input].origfunc(L);
}

int secure_io_output(lua_State* L)
{
  if (auto fn = lua_tostring(L, 1)) {
    std::string absFilename = base::get_absolute_path(fn);

    if (!ask_access(L, absFilename.c_str(), FileAccessMode::Write, ResourceType::File)) {
      return luaL_error(L, "the script doesn't have access to file '%s'", absFilename.c_str());
    }
  }
  return replaced_functions[io_output].origfunc(L);
}

int secure_os_execute(lua_State* L)
{
  const char* cmd = luaL_checkstring(L, 1);
  if (!ask_access(L, cmd, FileAccessMode::Execute, ResourceType::Command)) {
    // Stop script
    return luaL_error(L, "the script doesn't have access to execute the command: '%s'", cmd);
  }
  return replaced_functions[os_execute].origfunc(L);
}

int file_result(lua_State* L, bool result, int errorNo = 0, const std::string& fileName = "")
{
  if (result) {
    lua_pushboolean(L, 1);
    return 1;
  }

  luaL_pushfail(L);
  if (fileName.empty())
    lua_pushstring(L, strerror(errorNo));
  else
    lua_pushfstring(L, "%s: %s", fileName.c_str(), strerror(errorNo));
  lua_pushinteger(L, errorNo);
  return 3;
}

int secure_os_remove(lua_State* L)
{
  const std::string absFilename = base::get_canonical_path(luaL_checkstring(L, 1));
  if (absFilename.empty())
    return file_result(L, false, ENOENT, absFilename);

  if (!ask_access(L, absFilename.data(), FileAccessMode::Write, ResourceType::File))
    return file_result(L, false, EACCES, absFilename);

  if (base::is_directory(absFilename)) {
    try {
      base::remove_directory(absFilename);
      return file_result(L, true);
    }
    catch (const std::exception&) {
      return file_result(L, false, EIO, absFilename);
    }
  }

  try {
    base::delete_file(absFilename);
  }
  catch (const std::exception&) {
    return file_result(L, false, EIO, absFilename);
  }

  return file_result(L, true);
}

int secure_os_rename(lua_State* L)
{
  const std::string absSourceFilename = base::get_canonical_path(luaL_checkstring(L, 1));
  const std::string absDestFilename = base::get_absolute_path(luaL_checkstring(L, 2));
  lua_pop(L, 2);

  if (absSourceFilename.empty())
    return file_result(L, false, ENOENT, absSourceFilename);

  if (absDestFilename.empty())
    return file_result(L, false, EINVAL, absDestFilename);

  if (!ask_access(L, absSourceFilename.data(), FileAccessMode::Write, ResourceType::File))
    return file_result(L, false, EACCES, absSourceFilename);

  try {
    // If the destination file already exists, we should ask for permission to overwrite it.
    if (!base::get_canonical_path(absDestFilename).empty() &&
        !ask_access(L, absDestFilename.data(), FileAccessMode::Write, ResourceType::File)) {
      return file_result(L, false, EACCES, absDestFilename);
    }

    base::move_file(absSourceFilename, absDestFilename);
    return file_result(L, true);
  }
  catch (const std::exception&) {
    return file_result(L, false, EIO, absSourceFilename);
  }
}

int secure_package_loadlib(lua_State* L)
{
  const char* cmd = luaL_checkstring(L, 1);
  if (!ask_access(L, cmd, FileAccessMode::LoadLib, ResourceType::File)) {
    // Stop script
    return luaL_error(L, "the script doesn't have access to execute the command: '%s'", cmd);
  }
  return replaced_functions[package_loadlib].origfunc(L);
}

// The app loads text chunks only: a precompiled chunk can do what no
// script can, such as reading and writing memory it doesn't own. The
// mode argument at index mode is "t", and refused if it asks for "b".
void text_mode(lua_State* L, int mode)
{
  if (!lua_isnoneornil(L, mode) && std::strchr(luaL_checkstring(L, mode), 'b'))
    luaL_error(L, "the app loads text chunks only, not mode '%s'", lua_tostring(L, mode));
  if (lua_gettop(L) < mode)
    lua_settop(L, mode);
  lua_pushliteral(L, "t");
  lua_replace(L, mode);
}

int text_load(lua_State* L)
{
  text_mode(L, 3);
  return replaced_functions[base_load].origfunc(L);
}

int text_loadfile(lua_State* L)
{
  text_mode(L, 2);
  return replaced_functions[base_loadfile].origfunc(L);
}

// package.searchers[2]: a Lua module from package.path, as a text
// chunk. The upvalues are the package table and package.searchpath().
int text_lua_searcher(lua_State* L)
{
  const char* name = luaL_checkstring(L, 1);
  if (lua_getfield(L, lua_upvalueindex(1), "path") != LUA_TSTRING)
    return luaL_error(L, "'package.path' must be a string");
  lua_pushvalue(L, lua_upvalueindex(2));
  lua_pushvalue(L, 1);
  lua_pushvalue(L, -3);
  lua_call(L, 2, 2);
  if (lua_isnil(L, -2))
    return 1; // Where it looked
  lua_pop(L, 1);
  const char* filename = lua_tostring(L, -1);
  if (luaL_loadfilex(L, filename, "t") != LUA_OK)
    return luaL_error(L,
                      "error loading module '%s' from file '%s':\n\t%s",
                      name,
                      filename,
                      lua_tostring(L, -1));
  lua_insert(L, -2);
  return 2;
}

// The level of the stack that require() was called from, for
// ask_access(): the first Lua function that isn't the app's own
// require()
int require_caller_level(lua_State* L)
{
  lua_Debug ar;
  for (int level = 0; lua_getstack(L, level, &ar); ++level) {
    if (lua_getinfo(L, "S", &ar) && *ar.what != 'C' && std::strcmp(ar.source, "internal") != 0)
      return level + 1;
  }
  return 0;
}

// package.searchers[3] and [4]: a C module from the package.cpath the
// app started with, which no script changes, loaded only with the
// access to load libraries that package.loadlib() asks for. The
// upvalues are that cpath, package.searchpath() and whether the
// module is the root of the name (searchers[4]).
int fixed_c_searcher(lua_State* L)
{
  const std::string name = luaL_checkstring(L, 1);
  std::string lookup = name;
  const bool root = lua_toboolean(L, lua_upvalueindex(3));
  if (root) {
    const size_t dot = name.find('.');
    if (dot == std::string::npos)
      return 0;
    lookup.resize(dot);
  }
  lua_pushvalue(L, lua_upvalueindex(2));
  lua_pushstring(L, lookup.c_str());
  lua_pushvalue(L, lua_upvalueindex(1));
  lua_call(L, 2, 2);
  if (lua_isnil(L, -2))
    return 1; // Where it looked
  lua_pop(L, 1);
  const std::string filename = lua_tostring(L, -1);

  if (!ask_access(L,
                  filename.c_str(),
                  FileAccessMode::LoadLib,
                  ResourceType::File,
                  require_caller_level(L)))
    return luaL_error(L, "the script doesn't have access to load the library '%s'", filename.c_str());

  // The open function, as Lua names it: luaopen_ and the name with
  // "_" for ".", first without what follows a "-", then without what
  // precedes it
  std::string mod = name;
  std::replace(mod.begin(), mod.end(), '.', '_');
  std::vector<std::string> names;
  const size_t mark = mod.find('-');
  if (mark != std::string::npos) {
    names.push_back(mod.substr(0, mark));
    mod = mod.substr(mark + 1);
  }
  names.push_back(mod);
  for (const std::string& n : names) {
    lua_pushcfunction(L, replaced_functions[package_loadlib].origfunc);
    lua_pushstring(L, filename.c_str());
    lua_pushstring(L, ("luaopen_" + n).c_str());
    lua_call(L, 2, 3);
    if (lua_isfunction(L, -3)) {
      lua_pop(L, 2);
      lua_pushstring(L, filename.c_str());
      return 2;
    }
    const bool noFunction = (lua_tostring(L, -1) && std::strcmp(lua_tostring(L, -1), "init") == 0);
    if (!noFunction || (&n == &names.back() && !root))
      return luaL_error(L,
                        "error loading module '%s' from file '%s':\n\t%s",
                        name.c_str(),
                        filename.c_str(),
                        lua_tostring(L, -2));
    lua_pop(L, 3);
  }
  lua_pushfstring(L, "no module '%s' in file '%s'", name.c_str(), filename.c_str());
  return 1;
}

} // anonymous namespace

void overwrite_unsecure_functions(lua_State* L)
{
  // Remove unsupported functions
  lua_getglobal(L, "os");
  for (const char* name : { "exit", "tmpname" }) {
    lua_pushcfunction(L, unsupported);
    lua_setfield(L, -2, name);
  }
  lua_pop(L, 1);

  // Without these a script could read or change the locals, upvalues
  // and objects of another one, and steer a trusted function
  lua_getglobal(L, "debug");
  for (const char* name : { "getlocal",
                            "setlocal",
                            "getupvalue",
                            "setupvalue",
                            "upvaluejoin",
                            "sethook",
                            "getregistry",
                            "getmetatable",
                            "setmetatable",
                            "getuservalue",
                            "setuservalue" }) {
    lua_pushcfunction(L, unsupported);
    lua_setfield(L, -2, name);
  }
  lua_pop(L, 1);

  // Replace functions with our own implementations (that ask for
  // permissions first).
  for (auto& item : replaced_functions) {
    lua_getglobal(L, item.package);

    // Get old function
    if (!item.origfunc) {
      lua_getfield(L, -1, item.funcname);
      item.origfunc = lua_tocfunction(L, -1);
      lua_pop(L, 1);
    }

    // Push and set the new function
    lua_pushcfunction(L, item.newfunc);
    lua_setfield(L, -2, item.funcname);

    lua_pop(L, 1);
  }

  // require() loads text chunks, and C modules only from where the
  // app looks for them and with access
  lua_getglobal(L, LUA_LOADLIBNAME);
  const int package = lua_gettop(L);
  lua_getfield(L, package, "searchers");
  lua_pushvalue(L, package);
  lua_getfield(L, package, "searchpath");
  lua_pushcclosure(L, text_lua_searcher, 2);
  lua_rawseti(L, -2, 2);
  for (int i : { 3, 4 }) {
    lua_getfield(L, package, "cpath");
    lua_getfield(L, package, "searchpath");
    lua_pushboolean(L, i == 4);
    lua_pushcclosure(L, fixed_c_searcher, 3);
    lua_rawseti(L, -2, i);
  }
  lua_pop(L, 2);
}

bool ask_access(lua_State* L,
                const char* filename,
                const FileAccessMode mode,
                const ResourceType resourceType,
                const int stackLevel)
{
  // Ask for permission to open the file
  if (App::instance()->context()->isUIAvailable()) {
    const std::string script = get_script_filename(L, stackLevel);
    if (script.empty()) {
      // No script
      luaL_error(L, "no debug information (script filename) to secure io.open() call");
      return false;
    }

    // A script that comes with the app, as long as no other script is
    // part of the call
    if (is_trusted_access(L, mode, filename))
      return true;

    const char* section = "script_access";
    std::string key = get_key(script);

    int access = get_config_int(section, key.c_str(), 0);

    // Has the correct access
    if ((access & int(mode)) == int(mode))
      return true;

    std::string allowButtonText;
    switch (mode) {
      case FileAccessMode::LoadLib:
        allowButtonText = Strings::script_access_allow_load_lib_access();
        break;
      case FileAccessMode::OpenSocket:
        allowButtonText = Strings::script_access_allow_open_conn_access();
        break;
      case FileAccessMode::Execute:
        allowButtonText = Strings::script_access_allow_execute_access();
        break;
      case FileAccessMode::Write:
        allowButtonText = Strings::script_access_allow_write_access();
        break;
      case FileAccessMode::Read:
        allowButtonText = Strings::script_access_allow_read_access();
        break;
      default: {
        luaL_error(L, "invalid access request");
        return false;
      }
    }

    app::gen::ScriptAccess dlg;
    dlg.script()->setText(script);

    {
      std::string label;
      switch (resourceType) {
        case ResourceType::File: {
          if (mode == FileAccessMode::Write) {
            label = Strings::script_access_file_write_label();
          }
          else {
            label = Strings::script_access_file_label();
          }
          break;
        }
        case ResourceType::Command:   label = Strings::script_access_command_label(); break;
        case ResourceType::WebSocket: label = Strings::script_access_websocket_label(); break;
        case ResourceType::Clipboard: label = Strings::script_access_clipboard_label(); break;
      }
      dlg.fileLabel()->setText(label);
    }

    if (filename && strlen(filename) > 0)
      dlg.file()->setText(filename);
    else
      dlg.fileContainer()->setVisible(false);

    dlg.allow()->setText(allowButtonText);
    dlg.allow()->processMnemonicFromText();

    if (script == "internal") {
      // This should not happen, we should have a proper script
      // filename here, probably the given "stackLevel" is wrong.
      ASSERT(false);

      // Make it look like a normal label
      dlg.script()->setType(ui::WidgetType::kLabelWidget);
      dlg.script()->initTheme();
    }
    else
      dlg.script()->Click.connect([&script] { app::launcher::open_folder(script); });

    dlg.full()->Click.connect([&dlg, &allowButtonText]() {
      if (dlg.full()->isSelected()) {
        dlg.dontShow()->setSelected(true);
        dlg.dontShow()->setEnabled(false);
        dlg.allow()->setText(Strings::script_access_give_full_access());
        dlg.allow()->processMnemonicFromText();
        dlg.layout();
      }
      else {
        dlg.dontShow()->setEnabled(true);
        dlg.allow()->setText(allowButtonText);
        dlg.allow()->processMnemonicFromText();
        dlg.layout();
      }
    });

    if (resourceType == ResourceType::File) {
      const std::string fn = dlg.file()->text();
      dlg.file()->Click.connect([fn] {
        if (base::is_file(fn))
          app::launcher::open_folder(fn);
        else
          app::launcher::open_folder(base::get_file_path(fn));
      });
    }

    // A long script name or resource breaks into lines, so the alert
    // fits within the window
    const int room = ui::Manager::getDefault()->bounds().w - 2 * kAlertMargin * ui::guiscale();
    const int excess = dlg.sizeHint().w - room;
    if (excess > 0) {
      int widest = 0;
      for (ui::LinkLabel* link : { dlg.script(), dlg.file() })
        widest = std::max(widest, link->textWidth());
      for (ui::LinkLabel* link : { dlg.script(), dlg.file() }) {
        const std::vector<std::string> lines = break_lines(
          link->text(),
          alert_line_width(widest, excess),
          [link](const std::string& s) { return int(std::ceil(link->font()->textLength(s))); });
        link->setText(lines[0]);
        for (size_t i = 1; i < lines.size(); ++i) {
          auto line = new ui::LinkLabel("", lines[i]);
          line->Click.connect([link] { link->Click(); });
          link->parent()->addChild(line);
        }
      }
    }

    dlg.openWindowInForeground();
    const bool allow = (dlg.closer() == dlg.allow());

    // Save selected option
    if (allow && dlg.dontShow()->isSelected()) {
      if (dlg.full()->isSelected())
        set_config_int(section, key.c_str(), access | int(FileAccessMode::Full));
      else
        set_config_int(section, key.c_str(), access | int(mode));
      flush_config_file();
    }

    if (!allow)
      return false;
  }
  return true;
}

int alert_line_width(int widest, int excess)
{
  return std::max(160 * ui::guiscale(), widest - excess);
}

std::vector<std::string> break_lines(const std::string& text,
                                     int width,
                                     const std::function<int(const std::string&)>& measure)
{
  // The end of the UTF-8 character that starts at i
  auto next = [](const std::string& s, size_t i) {
    ++i;
    while (i < s.size() && (s[i] & 0xC0) == 0x80)
      ++i;
    return i;
  };

  std::vector<std::string> lines;
  std::string rest = text;
  while (!rest.empty() && measure(rest) > width) {
    size_t fit = next(rest, 0);
    size_t brk = 0;
    for (size_t end = fit; end < rest.size() && measure(rest.substr(0, end)) <= width;
         end = next(rest, end)) {
      fit = end;
      if (rest[end - 1] == '/' || rest[end - 1] == ' ')
        brk = end;
    }
    const size_t cut = (brk ? brk : fit);
    lines.push_back(rest.substr(0, cut));
    rest.erase(0, cut);
  }
  if (!rest.empty() || lines.empty())
    lines.push_back(rest);
  return lines;
}

lua_CFunction get_original_io_open()
{
  return replaced_functions[io_open].origfunc;
}

}} // namespace app::script
