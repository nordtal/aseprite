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
using TreeRows = std::vector<std::pair<TreeNode*, int>>; // Visible nodes and their depth

// The nodes of a tree, its visible rows and its selection, without
// the widget that paints them.
class TreeModel {
public:
  // Replaces the children of the given node, or the root nodes if
  // it's null. The selection survives if its node ID survives.
  void setChildren(TreeNode* parent, TreeNodes&& children);

  TreeNode* findNode(const std::string& id) const;

  // Rebuilds the rows after the fields of a node were changed
  // directly.
  void nodeChanged();

  const TreeRows& rows() const { return m_rows; }

  TreeNode* selected() const { return m_selected; }
  void setSelected(TreeNode* node);

  // What the user does. Each one fires the events of its change, and
  // a callback may replace any node, the given one too.
  void select(TreeNode* node);
  void toggle(TreeNode* node);
  void contextMenu(TreeNode* node);

  // Events from the user, programmatic changes don't fire them.
  obs::signal<void(TreeNode*)> Change;
  obs::signal<void(TreeNode*, bool)> Expand;
  obs::signal<void(TreeNode*)> ContextMenu;

  // The rows changed
  obs::signal<void()> RowsChange;
  // The row at the given index has to be visible
  obs::signal<void(int)> ShowRow;

private:
  void unindex(TreeNode* node, std::map<std::string, bool>& oldExpanded);
  void index(TreeNode* node, const std::map<std::string, bool>& oldExpanded);
  void addRows(const TreeNodes& nodes, int depth);
  TreeNode* again(TreeNode* node, const std::string& id, int generation) const;

  TreeNodes m_nodes;
  std::map<std::string, TreeNode*> m_byId;
  TreeRows m_rows;
  TreeNode* m_selected = nullptr;
  // Counts the calls to setChildren(), which free nodes
  int m_generation = 0;
};

// A scrollable tree of nodes with a label, an icon and a badge each.
class Tree : public ui::View {
public:
  static ui::WidgetType Type();

  Tree();

  TreeModel& model() { return m_model; }
  const TreeModel& model() const { return m_model; }

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override;

private:
  class Content;

  TreeModel m_model;
  Content* m_content;
};

}} // namespace app::script

#endif
