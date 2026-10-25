// Aseprite
// Copyright (C) 2026  Nordtal
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/script/tree_widget.h"

#include "app/ui/skin/skin_theme.h"
#include "os/surface.h"
#include "text/font.h"
#include "ui/graphics.h"
#include "ui/message.h"
#include "ui/paint_event.h"
#include "ui/scale.h"
#include "ui/size_hint_event.h"
#include "ui/system.h"

#include <algorithm>
#include <cmath>

namespace app { namespace script {

using namespace ui;
using namespace app::skin;

// The widget inside the view that paints the visible rows.
class Tree::Content : public Widget {
public:
  Content(Tree* tree) : m_tree(tree) { setFocusStop(true); }

  int rowHeight() const { return int(font()->lineHeight()) + 4 * guiscale(); }

  int indent() const { return 12 * guiscale(); }

  gfx::Rect rowBounds(int row) const
  {
    const gfx::Rect rc = clientBounds();
    return gfx::Rect(rc.x, rc.y + row * rowHeight(), rc.w, rowHeight());
  }

  void makeRowVisible(int row)
  {
    View* view = m_tree;
    const gfx::Rect vp = view->viewportBounds();
    gfx::Point scroll = view->viewScroll();
    const int y = row * rowHeight();
    if (y < scroll.y)
      scroll.y = y;
    else if (y + rowHeight() > scroll.y + vp.h)
      scroll.y = y + rowHeight() - vp.h;
    view->setViewScroll(scroll);
  }

protected:
  void onSizeHint(SizeHintEvent& ev) override
  {
    // Wide enough for the widest visible row
    auto theme = SkinTheme::get(this);
    const int pad = 2 * guiscale();
    int w = 0;
    for (const auto& [node, depth] : m_tree->m_rows) {
      int rowW = 2 * pad + (depth + 1) * indent() + int(std::ceil(font()->textLength(node->text)));
      if (!node->icon.empty()) {
        if (SkinPartPtr part = theme->getPartById(node->icon))
          rowW += part->bitmap(0)->width() + pad;
      }
      if (!node->badge.empty())
        rowW += int(std::ceil(font()->textLength(node->badge))) + 4 * pad;
      w = std::max(w, rowW);
    }
    ev.setSizeHint(gfx::Size(w, rowHeight() * int(m_tree->m_rows.size())));
  }

  void onPaint(PaintEvent& ev) override
  {
    Graphics* g = ev.graphics();
    auto theme = SkinTheme::get(this);
    g->fillRect(theme->colors.listitemNormalFace(), clientBounds());

    const int h = rowHeight();
    const int pad = 2 * guiscale();
    for (int row = 0; row < int(m_tree->m_rows.size()); ++row) {
      const gfx::Rect rc = rowBounds(row);
      if ((g->getClipBounds() & rc).isEmpty())
        continue;

      const auto [node, depth] = m_tree->m_rows[row];
      const bool selected = (node == m_tree->m_selected);
      const gfx::Color bg = (selected ? theme->colors.listitemSelectedFace() :
                                        theme->colors.listitemNormalFace());
      const gfx::Color fg = (selected ? theme->colors.listitemSelectedText() :
                                        theme->colors.listitemNormalText());
      g->fillRect(bg, rc);

      int x = rc.x + pad + depth * indent();

      // Expander arrow
      if (!node->children.empty()) {
        SkinPartPtr part = (node->expanded ? theme->parts.comboboxArrowDown() :
                                             theme->parts.comboboxArrowRight());
        if (part) {
          os::Surface* arrow = part->bitmap(0);
          g->drawColoredRgbaSurface(arrow,
                                    fg,
                                    x + (indent() - arrow->width()) / 2,
                                    rc.y + (h - arrow->height()) / 2);
        }
      }
      x += indent();

      // Icon
      if (!node->icon.empty()) {
        if (SkinPartPtr part = theme->getPartById(node->icon)) {
          os::Surface* icon = part->bitmap(0);
          g->drawRgbaSurface(icon, x, rc.y + (h - icon->height()) / 2);
          x += icon->width() + pad;
        }
      }

      // Badge
      int right = rc.x2() - pad;
      if (!node->badge.empty()) {
        const gfx::Size sz = g->measureText(node->badge);
        const gfx::Rect badge(right - sz.w - 2 * pad, rc.y + guiscale(), sz.w + 2 * pad, h - 2 * guiscale());
        const gfx::Color badgeBg = (selected ? theme->colors.listitemSelectedText() :
                                               theme->colors.listitemSelectedFace());
        const gfx::Color badgeFg = (selected ? theme->colors.listitemSelectedFace() :
                                               theme->colors.listitemSelectedText());
        g->fillRect(badgeBg, badge);
        g->drawText(node->badge,
                    badgeFg,
                    badgeBg,
                    gfx::Point(badge.x + pad, rc.y + (h - sz.h) / 2));
        right = badge.x - pad;
      }

      // Label
      const gfx::Size sz = g->measureText(node->text);
      IntersectClip clip(g, gfx::Rect(x, rc.y, std::max(0, right - x), h));
      if (clip)
        g->drawText(node->text, fg, bg, gfx::Point(x, rc.y + (h - sz.h) / 2));
    }
  }

  bool onProcessMessage(Message* msg) override
  {
    switch (msg->type()) {
      case kMouseDownMessage:
      case kDoubleClickMessage: {
        auto mouseMsg = static_cast<MouseMessage*>(msg);
        const gfx::Point pos = mouseMsg->position() - bounds().origin();
        const int row = pos.y / rowHeight();
        if (pos.y < 0 || row >= int(m_tree->m_rows.size()))
          break;
        requestFocus();

        const auto [node, depth] = m_tree->m_rows[row];
        const int arrowX = 2 * guiscale() + depth * indent();
        const bool onArrow = (pos.x >= arrowX && pos.x < arrowX + indent());

        if (mouseMsg->right()) {
          m_tree->select(node);
          m_tree->ContextMenu(node);
        }
        else if (!node->children.empty() && (onArrow || msg->type() == kDoubleClickMessage)) {
          m_tree->toggle(node);
        }
        else {
          m_tree->select(node);
        }
        return true;
      }

      case kKeyDownMessage: {
        if (!hasFocus())
          break;
        auto& rows = m_tree->m_rows;
        int row = -1;
        for (int i = 0; i < int(rows.size()); ++i)
          if (rows[i].first == m_tree->m_selected)
            row = i;
        const auto scancode = static_cast<KeyMessage*>(msg)->scancode();
        TreeNode* node = (row >= 0 ? rows[row].first : nullptr);
        switch (scancode) {
          case kKeyUp:
            if (row > 0)
              m_tree->select(rows[row - 1].first);
            return true;
          case kKeyDown:
            if (row + 1 < int(rows.size()))
              m_tree->select(rows[row + 1].first);
            return true;
          case kKeyLeft:
            if (node && node->expanded)
              m_tree->toggle(node);
            else if (node && node->parent)
              m_tree->select(node->parent);
            return true;
          case kKeyRight:
            if (node && !node->expanded && !node->children.empty())
              m_tree->toggle(node);
            return true;
          default: break;
        }
        break;
      }
    }
    return Widget::onProcessMessage(msg);
  }

private:
  Tree* m_tree;
};

// static
ui::WidgetType Tree::Type()
{
  static ui::WidgetType type = ui::kGenericWidget;
  if (type == ui::kGenericWidget)
    type = ui::register_widget_type();
  return type;
}

Tree::Tree() : m_content(new Content(this))
{
  setType(Type());
  attachToView(m_content);
  setExpansive(true);
  InitTheme.connect([this] {
    auto theme = SkinTheme::get(this);
    setStyle(theme->styles.view());
  });
  initTheme();
}

void Tree::onSizeHint(SizeHintEvent& ev)
{
  // A view asks for no room for its content, the tree asks for its
  // width and leaves the height to the dialog
  View::onSizeHint(ev);
  gfx::Size sz = ev.sizeHint();
  sz.w += m_content->sizeHint().w + theme()->getScrollbarSize();
  ev.setSizeHint(sz);
}

void Tree::unindex(TreeNode* node, std::map<std::string, bool>& oldExpanded)
{
  if (node == m_selected)
    m_selected = nullptr;
  oldExpanded[node->id] = node->expanded;
  m_byId.erase(node->id);
  for (auto& child : node->children)
    unindex(child.get(), oldExpanded);
}

void Tree::index(TreeNode* node, const std::map<std::string, bool>& oldExpanded)
{
  if (!node->expandedSet) {
    auto it = oldExpanded.find(node->id);
    if (it != oldExpanded.end())
      node->expanded = it->second;
  }
  if (!node->id.empty())
    m_byId[node->id] = node;
  for (auto& child : node->children) {
    child->parent = node;
    index(child.get(), oldExpanded);
  }
}

void Tree::setChildren(TreeNode* parent, TreeNodes&& children)
{
  const std::string selectedId = (m_selected ? m_selected->id : std::string());
  std::map<std::string, bool> oldExpanded;
  TreeNodes& list = (parent ? parent->children : m_nodes);
  for (auto& node : list)
    unindex(node.get(), oldExpanded);

  list = std::move(children);
  for (auto& node : list) {
    node->parent = parent;
    index(node.get(), oldExpanded);
  }

  if (!m_selected && !selectedId.empty())
    m_selected = findNode(selectedId);
  nodeChanged();
}

TreeNode* Tree::findNode(const std::string& id) const
{
  auto it = m_byId.find(id);
  return (it != m_byId.end() ? it->second : nullptr);
}

void Tree::nodeChanged()
{
  rebuildRows();
  updateView();
  invalidate();
}

void Tree::setSelected(TreeNode* node)
{
  if (m_selected == node)
    return;
  m_selected = node;
  // Make the selected node visible
  for (TreeNode* p = (node ? node->parent : nullptr); p; p = p->parent)
    p->expanded = true;
  nodeChanged();
  for (int i = 0; i < int(m_rows.size()); ++i)
    if (m_rows[i].first == node)
      m_content->makeRowVisible(i);
}

void Tree::rebuildRows()
{
  m_rows.clear();
  addRows(m_nodes, 0);
}

void Tree::addRows(const TreeNodes& nodes, int depth)
{
  for (const auto& node : nodes) {
    m_rows.emplace_back(node.get(), depth);
    if (node->expanded)
      addRows(node->children, depth + 1);
  }
}

void Tree::toggle(TreeNode* node)
{
  node->expanded = !node->expanded;
  // A collapsed node takes the selection from its hidden children
  if (!node->expanded && m_selected) {
    for (TreeNode* p = m_selected->parent; p; p = p->parent) {
      if (p == node) {
        m_selected = node;
        Change(node);
        break;
      }
    }
  }
  nodeChanged();
  Expand(node, node->expanded);
}

void Tree::select(TreeNode* node)
{
  if (m_selected == node)
    return;
  setSelected(node);
  Change(node);
}

}} // namespace app::script
