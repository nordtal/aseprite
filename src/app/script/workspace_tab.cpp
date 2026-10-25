// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/script/workspace_tab.h"

#include "app/app.h"
#include "app/doc.h"
#include "app/ui/doc_view.h"
#include "app/ui/editor/editor.h"
#include "app/ui/main_window.h"
#include "app/ui/preview_editor.h"
#include "app/ui/status_bar.h"
#include "app/ui/workspace.h"
#include "app/ui_context.h"
#include "ui/app_state.h"

namespace app { namespace script {

WorkspaceTab::WorkspaceTab(ui::Widget* content, std::function<void()> onClose)
  : m_content(content)
  , m_onClose(std::move(onClose))
{
  setExpansive(true);
  m_content->setExpansive(true);
  addChild(m_content);

  // The workspace goes before the scripts' dialogs are closed
  m_exitConn = App::instance()->ExitGui.connect([this] { removeFromWorkspace(); });
}

WorkspaceTab::~WorkspaceTab()
{
  removeFromWorkspace();

  // The content belongs to the dialog
  removeChild(m_content);
}

Doc* WorkspaceTab::document() const
{
  // A closed document can still exist to be reopened, but it isn't
  // shown again.
  auto doc = doc::get<Doc>(m_docId);
  return (doc && doc->context() ? doc : nullptr);
}

void WorkspaceTab::setDocument(Doc* doc)
{
  m_docId = (doc ? doc->id() : doc::NullId);
  if (isOpen())
    updateDocView();
}

void WorkspaceTab::openTab()
{
  Workspace* workspace = App::instance()->workspace();
  if (!workspace)
    return;

  if (!isOpen())
    workspace->addView(this);
  updateDocView();
}

void WorkspaceTab::closeTab()
{
  if (!isOpen())
    return;

  // The embedded document goes back to a tab of its own
  if (DocView* view = docView()) {
    Doc* doc = view->document();
    delete view;
    UIContext::instance()->setActiveView(nullptr);
    UIContext::instance()->addDocView(doc);
  }
  App::instance()->workspace()->removeView(this);
}

gfx::Color WorkspaceTab::getTabColor()
{
  return gfx::ColorNone;
}

void WorkspaceTab::onWorkspaceViewSelected()
{
  if (DocView* view = docView())
    view->onWorkspaceViewSelected();
  else if (auto statusBar = StatusBar::instance())
    statusBar->showDefaultText();
}

bool WorkspaceTab::onCloseView(Workspace* workspace, bool quitting)
{
  // Closing in the tab closes its document first
  if (DocView* view = docView()) {
    if (!workspace->closeView(view, quitting))
      return false;
  }

  if (!m_closable)
    return false;

  m_onClose();
  return true;
}

InputChainElement* WorkspaceTab::onGetInputChainElement()
{
  return docView();
}

DocView* WorkspaceTab::docView()
{
  // Only an embedded document view goes in the slot
  if (m_slot && m_slot->firstChild())
    return static_cast<DocView*>(m_slot->firstChild());
  return nullptr;
}

void WorkspaceTab::updateDocView()
{
  Workspace* workspace = App::instance()->workspace();
  UIContext* ctx = UIContext::instance();
  Doc* doc = document();
  DocView* old = docView();
  bool created = false;

  if (m_slot && (old ? old->document() : nullptr) != doc) {
    // The old document goes back to a tab of its own
    if (old) {
      Doc* oldDoc = old->document();
      delete old;
      ctx->setActiveView(nullptr);
      ctx->addDocView(oldDoc);
    }

    // The new one leaves its tabs
    if (doc) {
      for (DocView* view : ctx->getAllDocViews(doc)) {
        workspace->removeView(view);
        delete view;
      }

      auto view = new DocView(doc,
                              DocView::Normal,
                              App::instance()->mainWindow()->getPreviewEditor());
      view->setHost(this);
      view->setExpansive(true);
      m_slot->addChild(view);
      created = true;
    }

    // The context forgets the deleted views
    ctx->setActiveView(nullptr);
  }

  // Selecting the tab makes the context use its document
  workspace->setActiveView(this);

  if (created) {
    layout();
    docView()->editor()->setDefaultScroll();
  }
}

void WorkspaceTab::removeFromWorkspace()
{
  if (!isOpen())
    return;

  // While the app closes the document is closed with the others
  if (ui::get_app_state() != ui::AppState::kNormal) {
    delete docView();
    App::instance()->workspace()->removeView(this);
  }
  else
    closeTab();
}

}} // namespace app::script
