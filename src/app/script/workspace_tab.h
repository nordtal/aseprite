// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_SCRIPT_WORKSPACE_TAB_H_INCLUDED
#define APP_SCRIPT_WORKSPACE_TAB_H_INCLUDED
#pragma once

#include "app/ui/side_panel.h"
#include "app/ui/tabs.h"
#include "app/ui/workspace_view.h"
#include "doc/object_id.h"
#include "obs/connection.h"
#include "ui/box.h"

#include <functional>
#include <memory>

namespace app {
class Doc;

namespace script {

// A workspace tab that shows the content of a Dialog{ tab=true }. It
// can embed one document view, which takes the place of the
// document's own tabs while it's embedded.
class WorkspaceTab : public ui::VBox,
                     public TabView,
                     public WorkspaceView {
public:
  // The tab shows the content widget but doesn't own it. onClose is
  // called when the user closes the tab.
  WorkspaceTab(ui::Widget* content, std::function<void()> onClose);
  ~WorkspaceTab();

  void setClosable(bool closable) { m_closable = closable; }

  // The widget that holds the embedded document view.
  void setEditorSlot(ui::Widget* slot) { m_slot = slot; }
  ui::Widget* editorSlot() const { return m_slot; }

  // The embedded document. It's kept while the tab isn't open, and
  // embedded when it opens.
  Doc* document() const;
  void setDocument(Doc* doc);

  // Creates the side panel, which the tab owns.
  SidePanel* makeSidePanel();

  // Adds the tab to the workspace (or selects it), and removes it.
  void openTab();
  void closeTab();

  // TabView implementation
  std::string getTabText() override { return text(); }
  TabIcon getTabIcon() override { return TabIcon::NONE; }
  gfx::Color getTabColor() override;
  bool isTabClosable() override { return m_closable; }

  // WorkspaceView implementation
  ui::Widget* getContentWidget() override { return this; }
  void onWorkspaceViewSelected() override;
  bool onCloseView(Workspace* workspace, bool quitting) override;
  void onTabPopup(Workspace* workspace) override {}
  InputChainElement* onGetInputChainElement() override;
  DocView* docView() override;
  void removeEmbeddedView(DocView* view) override;
  SidePanel* sidePanel() override { return m_sidePanel.get(); }

  // The embedded document went away without the script: it was closed
  // in the tab, or another tab embedded it. Fired after the change.
  obs::signal<void()> DocumentChange;

private:
  bool isOpen() const { return parent() != nullptr; }
  void updateDocView();
  void removeFromWorkspace();

  ui::Widget* m_content;
  ui::Widget* m_slot = nullptr;
  std::function<void()> m_onClose;
  doc::ObjectId m_docId = doc::NullId;
  bool m_closable = true;
  std::unique_ptr<SidePanel> m_sidePanel;
  obs::scoped_connection m_exitConn;
  // Tells a deferred event whether the tab still exists
  std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
};

} // namespace script
} // namespace app

#endif
