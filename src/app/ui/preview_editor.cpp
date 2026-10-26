// Aseprite
// Copyright (C) 2018-2025  Igara Studio S.A.
// Copyright (C) 2001-2018  David Capello
//
// This program is distributed under the terms of
// the End-User License Agreement for Aseprite.

#ifdef HAVE_CONFIG_H
  #include "config.h"
#endif

#include "app/ui/preview_editor.h"

#include "app/app.h"
#include "app/doc.h"
#include "app/doc_event.h"
#include "app/i18n/strings.h"
#include "app/ini_file.h"
#include "app/loop_tag.h"
#include "app/modules/gui.h"
#include "app/pref/preferences.h"
#include "app/ui/editor/editor.h"
#include "app/ui/editor/editor_customization_delegate.h"
#include "app/ui/editor/editor_view.h"
#include "app/ui/editor/play_state.h"
#include "app/ui/skin/skin_theme.h"
#include "app/ui/toolbar.h"
#include "app/ui_context.h"
#include "doc/sprite.h"
#include "gfx/rect.h"
#include "os/window.h"
#include "ui/base.h"
#include "ui/button.h"
#include "ui/close_event.h"
#include "ui/display.h"
#include "ui/fit_bounds.h"
#include "ui/message.h"
#include "ui/system.h"

#include "doc/tag.h"

#include <algorithm>

namespace app {

using namespace app::skin;
using namespace ui;

class MiniCenterButton : public CheckBox {
public:
  MiniCenterButton() : CheckBox("")
  {
    setDecorative(true);
    setSelected(true);
    initTheme();
  }

protected:
  void onInitTheme(ui::InitThemeEvent& ev) override
  {
    CheckBox::onInitTheme(ev);

    auto theme = SkinTheme::get(this);
    setStyle(theme->styles.windowCenterButton());
  }

  void onSetDecorativeWidgetBounds() override
  {
    auto theme = SkinTheme::get(this);
    Widget* window = parent();
    const gfx::Size centerSize = this->sizeHint();
    const gfx::Size playSize = theme->calcSizeHint(this, theme->styles.windowPlayButton());
    const gfx::Size closeSize = theme->calcSizeHint(this, theme->styles.windowCloseButton());
    const gfx::Border margin = style()->margin();

    gfx::Rect rect(centerSize);
    rect.offset(window->bounds().x2() - theme->styles.windowCloseButton()->margin().width() -
                  closeSize.w - theme->styles.windowPlayButton()->margin().width() - playSize.w -
                  margin.right() - centerSize.w,
                window->bounds().y + margin.top());

    setBounds(rect);
  }

  bool onProcessMessage(Message* msg) override
  {
    switch (msg->type()) {
      case kSetCursorMessage: ui::set_mouse_cursor(kArrowCursor); return true;
    }

    return CheckBox::onProcessMessage(msg);
  }
};

class MiniPlayButton : public Button {
public:
  MiniPlayButton() : Button(""), m_isPlaying(false)
  {
    enableFlags(CTRL_RIGHT_CLICK);
    setDecorative(true);
    initTheme();
  }

  bool isPlaying() const { return m_isPlaying; }

  void setPlaying(bool state)
  {
    m_isPlaying = state;
    setupIcons();
    invalidate();
  }

  obs::signal<void()> Popup;

private:
  void onInitTheme(ui::InitThemeEvent& ev) override
  {
    Button::onInitTheme(ev);
    setupIcons();
  }

  void onClick() override
  {
    m_isPlaying = !m_isPlaying;
    setupIcons();

    Button::onClick();
  }

  void onSetDecorativeWidgetBounds() override
  {
    auto theme = SkinTheme::get(this);
    Widget* window = parent();
    const gfx::Size playSize = this->sizeHint();
    const gfx::Size closeSize = theme->calcSizeHint(this, theme->styles.windowCloseButton());
    const gfx::Border margin = style()->margin();

    gfx::Rect rect(playSize);
    rect.offset(window->bounds().x2() - theme->styles.windowCloseButton()->margin().width() -
                  closeSize.w - margin.right() - playSize.w,
                window->bounds().y + margin.top());

    setBounds(rect);
  }

  bool onProcessMessage(Message* msg) override
  {
    switch (msg->type()) {
      case kSetCursorMessage: ui::set_mouse_cursor(kArrowCursor); return true;

      case kMouseUpMessage:   {
        MouseMessage* mouseMsg = static_cast<MouseMessage*>(msg);
        if (mouseMsg->right()) {
          if (hasCapture()) {
            releaseMouse();
            Popup();

            setSelected(false);
            return true;
          }
        }
        break;
      }
    }

    return Button::onProcessMessage(msg);
  }

  void setupIcons()
  {
    auto theme = SkinTheme::get(this);
    if (m_isPlaying)
      setStyle(theme->styles.windowStopButton());
    else
      setStyle(theme->styles.windowPlayButton());
  }

  bool m_isPlaying;
};

PreviewEditorWindow::PreviewEditorWindow()
  : Window(WithTitleBar, Strings::preview_title())
  , m_docView(NULL)
  , m_centerButton(new MiniCenterButton())
  , m_playButton(new MiniPlayButton())
  , m_refFrame(0)
  , m_aniSpeed(1.0)
  , m_relatedEditor(nullptr)
  , m_opening(false)
{
  setAutoRemap(false);
  setWantFocus(false);

  m_isEnabled = get_config_bool("MiniEditor", "Enabled", true);

  m_centerButton->Click.connect([this] { onCenterClicked(); });
  m_playButton->Click.connect([this] { onPlayClicked(); });
  m_playButton->Popup.connect([this] { onPopupSpeed(); });

  addChild(m_centerButton);
  addChild(m_playButton);

  initTheme();
}

PreviewEditorWindow::~PreviewEditorWindow()
{
  set_config_bool("MiniEditor", "Enabled", m_isEnabled);
}

void PreviewEditorWindow::setPreviewEnabled(bool state)
{
  m_isEnabled = state;

  updateUsingEditor(Editor::activeEditor());
}

void PreviewEditorWindow::pressPlayButton()
{
  m_playButton->setPlaying(!m_playButton->isPlaying());
  onPlayClicked();
}

gfx::Rect default_preview_bounds(const gfx::Rect& editorView, const gfx::Size& size, int gap)
{
  return gfx::Rect(std::max(editorView.x, editorView.x2() - gap - size.w),
                   std::max(editorView.y, editorView.y2() - gap - size.h),
                   size.w,
                   size.h);
}

bool preview_place_changed(const gfx::Rect& openBounds,
                           const gfx::Rect& openFrame,
                           const gfx::Rect& closeBounds,
                           const gfx::Rect& closeFrame)
{
  return closeBounds != openBounds || closeFrame != openFrame;
}

bool PreviewEditorWindow::onProcessMessage(ui::Message* msg)
{
  switch (msg->type()) {
    case kOpenMessage: {
      Manager* manager = this->manager();
      Display* mainDisplay = manager->display();

      auto theme = SkinTheme::get(this);
      int extra = theme->dimensions.miniScrollbarSize();
      if (get_multiple_displays()) {
        extra *= mainDisplay->scale();
      }

      // Over the editor, whatever is docked around it
      gfx::Rect area = manager->bounds();
      if (View* view = (m_relatedEditor ? View::getView(m_relatedEditor) : nullptr))
        area = view->viewportBounds();
      gfx::Rect defaultBounds = default_preview_bounds(area, mainDisplay->size() / 4, extra);

      fit_bounds(mainDisplay, this, defaultBounds);

      load_window_pos(this, "PreviewWindow", false);
      invalidate();
      break;
    }

    case kCloseMessage:
      // A window that closes with the main window skips
      // Window::closeWindow(), which keeps the native frame
      if (ownDisplay())
        loadNativeFrame(display()->nativeWindow()->frame());
      if (preview_place_changed(m_openBounds, m_openFrame, bounds(), lastNativeFrame()))
        save_window_pos(this, "PreviewWindow");
      break;
  }

  return Window::onProcessMessage(msg);
}

void PreviewEditorWindow::onInitTheme(ui::InitThemeEvent& ev)
{
  Window::onInitTheme(ev);
  setChildSpacing(0);
}

void PreviewEditorWindow::onClose(ui::CloseEvent& ev)
{
  ButtonBase* closeButton = dynamic_cast<ButtonBase*>(ev.getSource());
  if (closeButton && closeButton->type() == kWindowCloseButtonWidget) {
    // Here we don't use "setPreviewEnabled" to change the state of
    // "m_isEnabled" because we're coming from a close event of the
    // window.
    m_isEnabled = false;

    // Redraw the tool bar because it shows the mini editor enabled
    // state. TODO abstract this event
    ToolBar::instance()->invalidate();

    destroyDocView();
  }
}

void PreviewEditorWindow::onWindowResize()
{
  Window::onWindowResize();

  DocView* view = UIContext::instance()->activeView();
  if (view)
    updateUsingEditor(view->editor());
}

void PreviewEditorWindow::onOpen(ui::Event& ev)
{
  Window::onOpen(ev);

  // The place it opened at, once a native window has its own display
  m_openBounds = bounds();
  m_openFrame = (ownDisplay() ? display()->nativeWindow()->frame() : gfx::Rect());
}

bool PreviewEditorWindow::hasDocument() const
{
  return (m_docView && m_docView->document() != nullptr);
}

DocumentPreferences& PreviewEditorWindow::docPref()
{
  Doc* doc = (m_docView ? m_docView->document() : nullptr);
  return Preferences::instance().document(doc);
}

void PreviewEditorWindow::onCenterClicked()
{
  if (!m_relatedEditor || !hasDocument())
    return;

  bool autoScroll = m_centerButton->isSelected();
  docPref().preview.autoScroll(autoScroll);
  if (autoScroll)
    updateUsingEditor(m_relatedEditor);
}

void PreviewEditorWindow::onPlayClicked()
{
  Editor* miniEditor = (m_docView ? m_docView->editor() : nullptr);
  if (!miniEditor || !miniEditor->document())
    return;

  if (m_playButton->isPlaying()) {
    m_refFrame = miniEditor->frame();
    miniEditor->play(Preferences::instance().preview.playOnce(),
                     Preferences::instance().preview.playAll(),
                     Preferences::instance().preview.playSubtags());
  }
  else {
    miniEditor->stop();
    if (m_relatedEditor)
      miniEditor->setFrame(m_relatedEditor->frame());
  }
}

void PreviewEditorWindow::onPopupSpeed()
{
  Editor* miniEditor = (m_docView ? m_docView->editor() : nullptr);
  if (!miniEditor || !miniEditor->document())
    return;

  miniEditor->showAnimationSpeedMultiplierPopup();
  m_aniSpeed = miniEditor->getAnimationSpeedMultiplier();
}

Editor* PreviewEditorWindow::previewEditor() const
{
  return (m_docView ? m_docView->editor() : nullptr);
}

void PreviewEditorWindow::updateUsingEditor(Editor* editor)
{
  if (m_opening)
    return;

  if (!m_isEnabled || !editor) {
    hideWindow();
    m_relatedEditor = nullptr;
    return;
  }

  if (!editor->isActive())
    return;

  m_relatedEditor = editor;

  Doc* document = editor->document();
  Editor* miniEditor = (m_docView ? m_docView->editor() : nullptr);

  if (!isVisible()) {
    m_opening = true;
    openWindow();
    m_opening = false;
  }

  // Document preferences used to store the preferred zoom/scroll point
  auto& docPref = Preferences::instance().document(document);
  bool autoScroll = docPref.preview.autoScroll();

  // Set the same location as in the given editor.
  if (!miniEditor || miniEditor->document() != document) {
    destroyDocView();

    m_docView = new DocView(document, DocView::Preview, this);
    addChild(m_docView);

    miniEditor = m_docView->editor();
    miniEditor->setZoom(render::Zoom::fromScale(docPref.preview.zoom()));
    miniEditor->setLayer(editor->layer());
    miniEditor->setFrame(editor->frame());
    miniEditor->setAnimationSpeedMultiplier(m_aniSpeed);
    miniEditor->add_observer(this);
    layout();

    if (!autoScroll)
      miniEditor->setEditorScroll(docPref.preview.scroll());
  }

  m_centerButton->setSelected(autoScroll);
  if (autoScroll) {
    gfx::Point centerPoint = editor->getVisibleSpriteBounds().center();
    miniEditor->centerInSpritePoint(centerPoint);

    saveScrollPref();
  }

  if (!m_playButton->isPlaying()) {
    miniEditor->stop();
    miniEditor->setLayer(editor->layer());
    miniEditor->setFrame(editor->frame());
  }
  else {
    adjustPlayingTag();
  }
}

void PreviewEditorWindow::uncheckCenterButton()
{
  if (m_centerButton->isSelected()) {
    m_centerButton->setSelected(false);
    onCenterClicked();
  }
}

void PreviewEditorWindow::onStateChanged(Editor* editor)
{
  // Sync editor playing state with MiniPlayButton state
  if (m_playButton->isPlaying() != editor->isPlaying())
    m_playButton->setPlaying(editor->isPlaying());
}

void PreviewEditorWindow::onScrollChanged(Editor* miniEditor)
{
  if (miniEditor->hasCapture()) {
    saveScrollPref();
    uncheckCenterButton();
  }
}

void PreviewEditorWindow::onZoomChanged(Editor* miniEditor)
{
  saveScrollPref();
}

void PreviewEditorWindow::saveScrollPref()
{
  ASSERT(m_docView);
  if (!m_docView)
    return;

  Editor* miniEditor = m_docView->editor();
  ASSERT(miniEditor);

  docPref().preview.scroll(View::getView(miniEditor)->viewScroll());
  docPref().preview.zoom(miniEditor->zoom().scale());
}

void PreviewEditorWindow::onScrollOtherEditor(Editor* editor)
{
  updateUsingEditor(editor);
}

void PreviewEditorWindow::onDisposeOtherEditor(Editor* editor)
{
  if (m_relatedEditor == editor)
    updateUsingEditor(nullptr);
}

void PreviewEditorWindow::onPreviewOtherEditor(Editor* editor)
{
  updateUsingEditor(editor);
}

void PreviewEditorWindow::onTagChangeEditor(Editor* editor, DocEvent& ev)
{
  if (m_playButton->isPlaying())
    adjustPlayingTag();
}

void PreviewEditorWindow::hideWindow()
{
  destroyDocView();
  if (isVisible())
    closeWindow(NULL);
}

void PreviewEditorWindow::destroyDocView()
{
  if (m_docView) {
    m_docView->editor()->remove_observer(this);

    delete m_docView;
    m_docView = nullptr;
  }
}

void PreviewEditorWindow::adjustPlayingTag()
{
  Editor* editor = m_relatedEditor;
  if (!editor || !m_docView)
    return;

  Editor* miniEditor = m_docView->editor();

  if (miniEditor->isPlaying()) {
    doc::Tag* tag =
      editor->getCustomizationDelegate()->getTagProvider()->getTagByFrame(editor->frame(), true);

    auto playState = dynamic_cast<PlayState*>(miniEditor->getState().get());
    doc::Tag* playingTag = (playState ? playState->playingTag() : nullptr);

    if (tag == playingTag)
      return;

    miniEditor->stop();
  }

  if (!miniEditor->isPlaying())
    miniEditor->setFrame(m_refFrame = editor->frame());

  miniEditor->play(Preferences::instance().preview.playOnce(),
                   Preferences::instance().preview.playAll(),
                   Preferences::instance().preview.playSubtags());
}

} // namespace app
