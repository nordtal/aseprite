// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_SCRIPT_TREE_WIDGET_H_INCLUDED
#define APP_SCRIPT_TREE_WIDGET_H_INCLUDED
#pragma once

#include "obs/signal.h"
#include "ui/view.h"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace app { namespace script {

// A node of the tree created with Dialog:tree{}.
struct TreeNode {
  std::string id;
  std::string text;
  std::string icon;  // ID of a theme part
  std::string badge; // Short text painted at the right of the row
  bool expanded = false;
  // False when the script didn't say, so a replaced node keeps the
  // expanded state of the node with the same ID.
  bool expandedSet = false;
  TreeNode* parent = nullptr;
  std::vector<std::unique_ptr<TreeNode>> children;
};

using TreeNodes = std::vector<std::unique_ptr<TreeNode>>;

// A scrollable tree of nodes with a label, an icon and a badge each.
class Tree : public ui::View {
public:
  static ui::WidgetType Type();

  Tree();

  // Replaces the children of the given node, or the root nodes if
  // it's null. The selection survives if its node ID survives.
  void setChildren(TreeNode* parent, TreeNodes&& children);

  TreeNode* findNode(const std::string& id) const;

  // Relayouts and repaints the tree after the fields of a node were
  // changed directly.
  void nodeChanged();

  TreeNode* selected() const { return m_selected; }
  void setSelected(TreeNode* node);

  // Events from the user, programmatic changes don't generate them.
  obs::signal<void(TreeNode*)> Change;
  obs::signal<void(TreeNode*, bool)> Expand;
  obs::signal<void(TreeNode*)> ContextMenu;

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override;

private:
  class Content;
  friend class Content;

  void unindex(TreeNode* node, std::map<std::string, bool>& oldExpanded);
  void index(TreeNode* node, const std::map<std::string, bool>& oldExpanded);
  void rebuildRows();
  void addRows(const TreeNodes& nodes, int depth);
  void toggle(TreeNode* node);
  void select(TreeNode* node);

  Content* m_content;
  TreeNodes m_nodes;
  std::map<std::string, TreeNode*> m_byId;
  std::vector<std::pair<TreeNode*, int>> m_rows; // Visible nodes and their depth
  TreeNode* m_selected = nullptr;
};

}} // namespace app::script

#endif
