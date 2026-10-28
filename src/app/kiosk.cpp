// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/kiosk.h"

#include "app/app.h"
#include "app/commands/command.h"
#include "app/commands/command_ids.h"
#include "app/ui/home_view.h"
#include "app/ui/main_window.h"
#include "app/ui/workspace.h"
#include "os/window.h"
#include "ui/display.h"
#include "ui/manager.h"
#include "ui/system.h"

#include <algorithm>
#include <string>

namespace app { namespace kiosk {

static bool g_active = false;
static int g_allowed = 0;

bool active()
{
  return g_active;
}

void start()
{
  g_active = true;

  Workspace* workspace = App::instance()->workspace();
  if (!workspace)
    return;

  for (WorkspaceView* view : *workspace) {
    if (dynamic_cast<HomeView*>(view)) {
      workspace->removeView(view);
      break;
    }
  }

  // After the manager, which sets the drag target once its first
  // messages run
  ui::execute_from_ui_thread([] {
    if (ui::Manager* manager = ui::Manager::getDefault())
      if (os::Window* window = manager->display()->nativeWindow())
        window->setDragTarget(nullptr);
  });
}

bool refuses(const Command* command)
{
  static const char* const ids[] = {
    CommandId::NewFile(),     CommandId::OpenFile(),
    CommandId::ReopenClosedFile(),
    CommandId::ImportSpriteSheet(),
    CommandId::NewSpriteFromSelection(),
    CommandId::DuplicateSprite(),
    CommandId::DuplicateView(),
    CommandId::CloseFile(),   CommandId::CloseAllFiles(),
    CommandId::GotoNextTab(), CommandId::GotoPreviousTab(),
    CommandId::Home(),
  };
  if (!g_active || g_allowed > 0)
    return false;
  return std::any_of(std::begin(ids), std::end(ids), [command](const char* id) {
    return command->id() == id;
  });
}

Allow::Allow()
{
  ++g_allowed;
}

Allow::~Allow()
{
  --g_allowed;
}

}} // namespace app::kiosk
