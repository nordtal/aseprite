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
