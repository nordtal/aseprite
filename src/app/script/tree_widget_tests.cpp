// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#include "tests/app_test.h"

#include "app/script/tree_widget.h"

using namespace app::script;

namespace {

std::unique_ptr<TreeNode> node(const std::string& id, TreeNodes children = {})
{
  auto n = std::make_unique<TreeNode>();
  n->id = id;
  n->expanded = !children.empty();
  n->children = std::move(children);
  return n;
}

// Folder "a" with the leaf "b"
TreeNodes nodes()
{
  TreeNodes children;
  children.push_back(node("b"));
  TreeNodes list;
  list.push_back(node("a", std::move(children)));
  return list;
}

} // anonymous namespace

// Each event carries the node that's in the tree when it fires, even
// if the callback of an event before it replaced every node, the way
// a script calls dlg:modify{ nodes } from onchange.

TEST(TreeModel, ContextMenuAfterChangeReplacedTheNodes)
{
  TreeModel model;
  model.setChildren(nullptr, nodes());
  model.Change.connect([&](TreeNode*) { model.setChildren(nullptr, nodes()); });
  TreeNode* menuNode = nullptr;
  model.ContextMenu.connect([&](TreeNode* n) { menuNode = n; });

  model.contextMenu(model.findNode("b"));
  ASSERT_NE(nullptr, menuNode);
  EXPECT_EQ(model.findNode("b"), menuNode);
  EXPECT_EQ(model.findNode("b"), model.selected());
}

TEST(TreeModel, ContextMenuAfterChangeRemovedTheNode)
{
  TreeModel model;
  model.setChildren(nullptr, nodes());
  model.Change.connect([&](TreeNode*) { model.setChildren(nullptr, {}); });
  int menus = 0;
  model.ContextMenu.connect([&](TreeNode*) { ++menus; });

  model.contextMenu(model.findNode("b"));
  EXPECT_EQ(0, menus);
}

TEST(TreeModel, CollapseAfterChangeReplacedTheNodes)
{
  TreeModel model;
  model.setChildren(nullptr, nodes());
  model.setSelected(model.findNode("b"));
  int changes = 0;
  model.Change.connect([&](TreeNode*) {
    ++changes;
    model.setChildren(nullptr, nodes());
  });
  TreeNode* expandNode = nullptr;
  bool expanded = true;
  model.Expand.connect([&](TreeNode* n, bool e) {
    expandNode = n;
    expanded = e;
  });

  // Collapsing "a" moves the selection from "b" to it
  model.toggle(model.findNode("a"));
  EXPECT_EQ(1, changes);
  ASSERT_NE(nullptr, expandNode);
  EXPECT_EQ(model.findNode("a"), expandNode);
  EXPECT_FALSE(expanded);
}

TEST(TreeModel, CollapseAfterChangeRemovedTheNode)
{
  TreeModel model;
  model.setChildren(nullptr, nodes());
  model.setSelected(model.findNode("b"));
  model.Change.connect([&](TreeNode*) { model.setChildren(nullptr, {}); });
  int expands = 0;
  model.Expand.connect([&](TreeNode*, bool) { ++expands; });

  model.toggle(model.findNode("a"));
  EXPECT_EQ(0, expands);
}

// A row of 1000 px in a tree that shows 100 px of it: the badge of 6
// px of text and 2 px of padding ends 2 px before the right edge of
// what the view shows, wherever it's scrolled, and the label ends 2
// px before the badge.

TEST(TreeRowLayout, FixedWidthUnscrolled)
{
  const TreeRowLayout layout =
    tree_row_layout(gfx::Rect(0, 32, 1000, 16), gfx::Rect(0, 0, 100, 80), 14, 6, 2);
  EXPECT_EQ(gfx::Rect(88, 33, 10, 14), layout.badge);
  EXPECT_EQ(gfx::Rect(14, 32, 72, 16), layout.label);
}

TEST(TreeRowLayout, FixedWidthScrolled)
{
  const TreeRowLayout layout =
    tree_row_layout(gfx::Rect(0, 32, 1000, 16), gfx::Rect(300, 0, 100, 80), 14, 6, 2);
  EXPECT_EQ(gfx::Rect(388, 33, 10, 14), layout.badge);
  EXPECT_EQ(gfx::Rect(14, 32, 372, 16), layout.label);
}

TEST(TreeRowLayout, FixedWidthScrolledToTheEnd)
{
  const TreeRowLayout layout =
    tree_row_layout(gfx::Rect(0, 32, 1000, 16), gfx::Rect(900, 0, 100, 80), 14, 6, 2);
  EXPECT_EQ(gfx::Rect(988, 33, 10, 14), layout.badge);
  EXPECT_EQ(gfx::Rect(14, 32, 972, 16), layout.label);
}

// A tree without a width shows its whole rows, or more, and paints the
// badge at the end of the row, as before

TEST(TreeRowLayout, NoWidth)
{
  EXPECT_EQ(gfx::Rect(188, 33, 10, 14),
            tree_row_layout(gfx::Rect(0, 32, 200, 16), gfx::Rect(0, 0, 200, 80), 14, 6, 2).badge);
  EXPECT_EQ(gfx::Rect(188, 33, 10, 14),
            tree_row_layout(gfx::Rect(0, 32, 200, 16), gfx::Rect(0, 0, 350, 80), 14, 6, 2).badge);
}

TEST(TreeRowLayout, NoBadge)
{
  const TreeRowLayout layout =
    tree_row_layout(gfx::Rect(0, 32, 1000, 16), gfx::Rect(300, 0, 100, 80), 14, 0, 2);
  EXPECT_TRUE(layout.badge.isEmpty());
  EXPECT_EQ(gfx::Rect(14, 32, 384, 16), layout.label);
}

// A label that starts past the badge isn't painted
TEST(TreeRowLayout, LabelPastTheBadge)
{
  const TreeRowLayout layout =
    tree_row_layout(gfx::Rect(0, 32, 1000, 16), gfx::Rect(0, 0, 20, 80), 14, 6, 2);
  EXPECT_EQ(0, layout.label.w);
}
