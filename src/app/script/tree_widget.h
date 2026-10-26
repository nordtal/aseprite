// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifndef APP_SCRIPT_TREE_WIDGET_H_INCLUDED
#define APP_SCRIPT_TREE_WIDGET_H_INCLUDED
#pragma once

#include "gfx/rect.h"
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

// Where a row paints its badge and its label. The badge sits at the
// right of the part of the row that the view shows, the label ends
// before it.
struct TreeRowLayout {
  gfx::Rect badge; // Empty without a badge
  gfx::Rect label;
};

// row and visible are in the coordinates of the tree's content,
// labelX is where the label starts, badgeTextWidth is 0 without a
// badge, pad is the space around the badge and its text.
TreeRowLayout tree_row_layout(const gfx::Rect& row,
                              const gfx::Rect& visible,
                              int labelX,
                              int badgeTextWidth,
                              int pad);

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

  // True when no node has children, so the rows reserve no column for
  // the expander arrow
  bool flat() const;

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

  // The width the tree asks for in pixels, or 0 to ask for the width
  // of its widest row. A wider row scrolls.
  void setPreferredWidth(int width);

protected:
  void onSizeHint(ui::SizeHintEvent& ev) override;
  void onSetViewScroll(const gfx::Point& pt) override;

private:
  class Content;

  TreeModel m_model;
  Content* m_content;
  int m_preferredWidth = 0;
};

}} // namespace app::script

#endif
