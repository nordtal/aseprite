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
#include "app/ui/workspace.h"

#include <algorithm>
#include <vector>

namespace app { namespace kiosk {

static bool g_active = false;

// The ids of the commands that an Allow lets run
static std::vector<const char*> g_allowed;

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
    CommandId::Home(),        CommandId::Screenshot(),
  };
  auto is = [command](const char* id) { return command->id() == id; };
  if (!g_active || std::any_of(g_allowed.begin(), g_allowed.end(), is))
    return false;
  return std::any_of(std::begin(ids), std::end(ids), is);
}

Allow::Allow(const char* commandId)
{
  g_allowed.push_back(commandId);
}

Allow::~Allow()
{
  g_allowed.pop_back();
}

}} // namespace app::kiosk
