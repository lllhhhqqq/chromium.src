// Copyright 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ui/views/sync/one_click_signin_bubble_view.h"

#include <memory>
#include <utility>

#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/macros.h"
#include "chrome/browser/ui/sync/one_click_signin_bubble_delegate.h"
#include "content/public/test/test_utils.h"
#include "ui/events/event_utils.h"
#include "ui/views/controls/button/label_button.h"
#include "ui/views/test/views_test_base.h"
#include "ui/views/widget/widget.h"

class OneClickSigninBubbleViewTest : public views::ViewsTestBase,
                                     public views::WidgetObserver {
 public:
  OneClickSigninBubbleViewTest() {}

  void SetUp() override {
    views::ViewsTestBase::SetUp();

    // Create a widget to host the anchor view.
    anchor_widget_ = new views::Widget;
    views::Widget::InitParams widget_params = CreateParams(
        views::Widget::InitParams::TYPE_WINDOW);
    anchor_widget_->Init(widget_params);
    anchor_widget_->Show();
  }

  void TearDown() override {
    OneClickSigninBubbleView::Hide();
    anchor_widget_->Close();
    anchor_widget_ = NULL;
    views::ViewsTestBase::TearDown();
  }

 protected:
  OneClickSigninBubbleView* ShowOneClickSigninBubble(
    BrowserWindow::OneClickSigninBubbleType bubble_type) {
    std::unique_ptr<OneClickSigninBubbleDelegate> delegate;
    delegate.reset(new OneClickSigninBubbleTestDelegate(this));

    OneClickSigninBubbleView::ShowBubble(
        bubble_type, base::string16(), base::string16(), std::move(delegate),
        anchor_widget_->GetContentsView(),
        base::Bind(&OneClickSigninBubbleViewTest::OnStartSync,
                   base::Unretained(this)));

    OneClickSigninBubbleView* view =
        OneClickSigninBubbleView::view_for_testing();
    EXPECT_TRUE(view != NULL);
    return view;
  }

  void OnStartSync(OneClickSigninSyncStarter::StartSyncMode mode) {
    on_start_sync_called_ = true;
    mode_ = mode;
  }

  // Waits for the OneClickSigninBubbleView to close, by observing its Widget,
  // then running a RunLoop, which OnWidgetDestroyed will quit. It's not
  // sufficient to wait for the message loop to go idle, as the bubble has a
  // closing animation which may still be running at that point. Instead, wait
  // for its widget to be destroyed.
  void WaitForClose() {
    views::Widget* widget =
        OneClickSigninBubbleView::view_for_testing()->GetWidget();
    widget->AddObserver(this);
    base::RunLoop run_loop;
    run_loop_ = &run_loop;
    // Block until OnWidgetDestroyed() is fired.
    run_loop.Run();
    // The Widget has been destroyed, so there's no need to remove this as an
    // observer.
  }

  // views::WidgetObserver method:
  void OnWidgetDestroyed(views::Widget* widget) override {
    run_loop_->Quit();
  }

  bool on_start_sync_called_ = false;
  OneClickSigninSyncStarter::StartSyncMode mode_ =
      OneClickSigninSyncStarter::CONFIGURE_SYNC_FIRST;
  int bubble_learn_more_click_count_ = 0;
  int dialog_learn_more_click_count_ = 0;
  int advanced_click_count_ = 0;

 private:
  friend class OneClickSigninBubbleTestDelegate;

  class OneClickSigninBubbleTestDelegate
      : public OneClickSigninBubbleDelegate {
   public:
    // |test| is not owned by this object.
    explicit OneClickSigninBubbleTestDelegate(
        OneClickSigninBubbleViewTest* test) : test_(test) {}

    // OneClickSigninBubbleDelegate:
    void OnLearnMoreLinkClicked(bool is_dialog) override {
      if (is_dialog)
        ++test_->dialog_learn_more_click_count_;
      else
        ++test_->bubble_learn_more_click_count_;
    }
    void OnAdvancedLinkClicked() override { ++test_->advanced_click_count_; }

   private:
    OneClickSigninBubbleViewTest* test_;

    DISALLOW_COPY_AND_ASSIGN(OneClickSigninBubbleTestDelegate);
  };

  // Widget to host the anchor view of the bubble. Destroys itself when closed.
  views::Widget* anchor_widget_ = nullptr;
  base::RunLoop* run_loop_ = nullptr;

  DISALLOW_COPY_AND_ASSIGN(OneClickSigninBubbleViewTest);
};

TEST_F(OneClickSigninBubbleViewTest, ShowBubble) {
  ShowOneClickSigninBubble(BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_BUBBLE);
  content::RunAllPendingInMessageLoop();
  EXPECT_TRUE(OneClickSigninBubbleView::IsShowing());
}

TEST_F(OneClickSigninBubbleViewTest, ShowDialog) {
  ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_MODAL_DIALOG);
  content::RunAllPendingInMessageLoop();
  EXPECT_TRUE(OneClickSigninBubbleView::IsShowing());
}

TEST_F(OneClickSigninBubbleViewTest, HideBubble) {
  ShowOneClickSigninBubble(BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_BUBBLE);

  OneClickSigninBubbleView::Hide();
  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
}

TEST_F(OneClickSigninBubbleViewTest, HideDialog) {
  ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_MODAL_DIALOG);

  OneClickSigninBubbleView::Hide();
  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
  EXPECT_TRUE(on_start_sync_called_);
  EXPECT_EQ(OneClickSigninSyncStarter::UNDO_SYNC, mode_);
}

TEST_F(OneClickSigninBubbleViewTest, BubbleOkButton) {
  OneClickSigninBubbleView* view =
    ShowOneClickSigninBubble(
      BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_BUBBLE);

  // Simulate pressing the OK button.  Set the message loop in the bubble
  // view so that it can be quit once the bubble is hidden.
  views::ButtonListener* listener = view;
  const ui::MouseEvent event(ui::ET_MOUSE_PRESSED, gfx::Point(), gfx::Point(),
                             ui::EventTimeForNow(), 0, 0);
  listener->ButtonPressed(view->ok_button_, event);

  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
}

TEST_F(OneClickSigninBubbleViewTest, DialogOkButton) {
  OneClickSigninBubbleView* view = ShowOneClickSigninBubble(
      BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_MODAL_DIALOG);

  // Simulate pressing the OK button.  Set the message loop in the bubble
  // view so that it can be quit once the bubble is hidden.
  views::ButtonListener* listener = view;
  const ui::MouseEvent event(ui::ET_MOUSE_PRESSED, gfx::Point(), gfx::Point(),
                             ui::EventTimeForNow(), 0, 0);
  listener->ButtonPressed(view->ok_button_, event);

  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
  EXPECT_TRUE(on_start_sync_called_);
  EXPECT_EQ(OneClickSigninSyncStarter::SYNC_WITH_DEFAULT_SETTINGS, mode_);
}

TEST_F(OneClickSigninBubbleViewTest, DialogUndoButton) {
  OneClickSigninBubbleView* view = ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_MODAL_DIALOG);

  // Simulate pressing the undo button.  Set the message loop in the bubble
  // view so that it can be quit once the bubble is hidden.
  views::ButtonListener* listener = view;
  const ui::MouseEvent event(ui::ET_MOUSE_PRESSED, gfx::Point(), gfx::Point(),
                             ui::EventTimeForNow(), 0, 0);
  listener->ButtonPressed(view->undo_button_, event);

  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
  EXPECT_TRUE(on_start_sync_called_);
  EXPECT_EQ(OneClickSigninSyncStarter::UNDO_SYNC, mode_);
}

TEST_F(OneClickSigninBubbleViewTest, BubbleAdvancedLink) {
  OneClickSigninBubbleView* view = ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_BUBBLE);

  // Simulate pressing a link in the bubble.
  views::LinkListener* listener = view;
  listener->LinkClicked(view->advanced_link_, 0);

  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
  EXPECT_EQ(1, advanced_click_count_);
}

TEST_F(OneClickSigninBubbleViewTest, DialogAdvancedLink) {
  OneClickSigninBubbleView* view = ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_MODAL_DIALOG);

  // Simulate pressing a link in the bubble.
  views::LinkListener* listener = view;
  listener->LinkClicked(view->advanced_link_, 0);

  WaitForClose();
  EXPECT_TRUE(on_start_sync_called_);
  EXPECT_EQ(OneClickSigninSyncStarter::CONFIGURE_SYNC_FIRST, mode_);
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
  EXPECT_EQ(0, advanced_click_count_);
}

TEST_F(OneClickSigninBubbleViewTest, BubbleLearnMoreLink) {
  OneClickSigninBubbleView* view = ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_BUBBLE);

  views::LinkListener* listener = view;
  listener->LinkClicked(view->learn_more_link_, 0);

  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
  EXPECT_EQ(1, bubble_learn_more_click_count_);
  EXPECT_EQ(0, dialog_learn_more_click_count_);
}

TEST_F(OneClickSigninBubbleViewTest, DialogLearnMoreLink) {
  OneClickSigninBubbleView* view = ShowOneClickSigninBubble(
      BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_MODAL_DIALOG);

  views::LinkListener* listener = view;
  listener->LinkClicked(view->learn_more_link_, 0);

  // View should still be showing and the OnLearnMoreLinkClicked method
  // of the delegate should have been called with |is_dialog| == true.
  content::RunAllPendingInMessageLoop();
  EXPECT_TRUE(OneClickSigninBubbleView::IsShowing());
  EXPECT_EQ(0, bubble_learn_more_click_count_);
  EXPECT_EQ(1, dialog_learn_more_click_count_);
}

TEST_F(OneClickSigninBubbleViewTest, BubblePressEnterKey) {
  OneClickSigninBubbleView* one_click_view = ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_BUBBLE);

  // Simulate pressing the Enter key.
  views::View* view = one_click_view;
  const ui::Accelerator accelerator(ui::VKEY_RETURN, 0);
  view->AcceleratorPressed(accelerator);

  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
}

TEST_F(OneClickSigninBubbleViewTest, DialogPressEnterKey) {
  OneClickSigninBubbleView* one_click_view = ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_MODAL_DIALOG);

  // Simulate pressing the Enter key.
  views::View* view = one_click_view;
  const ui::Accelerator accelerator(ui::VKEY_RETURN, 0);
  view->AcceleratorPressed(accelerator);

  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
  EXPECT_TRUE(on_start_sync_called_);
  EXPECT_EQ(OneClickSigninSyncStarter::SYNC_WITH_DEFAULT_SETTINGS, mode_);
}

TEST_F(OneClickSigninBubbleViewTest, BubblePressEscapeKey) {
  OneClickSigninBubbleView* one_click_view = ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_BUBBLE);

  // Simulate pressing the Escape key.
  views::View* view = one_click_view;
  const ui::Accelerator accelerator(ui::VKEY_ESCAPE, 0);
  view->AcceleratorPressed(accelerator);

  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
}

TEST_F(OneClickSigninBubbleViewTest, DialogPressEscapeKey) {
  OneClickSigninBubbleView* one_click_view = ShowOneClickSigninBubble(
    BrowserWindow::ONE_CLICK_SIGNIN_BUBBLE_TYPE_MODAL_DIALOG);

  // Simulate pressing the Escape key.
  views::View* view = one_click_view;
  const ui::Accelerator accelerator(ui::VKEY_ESCAPE, 0);
  view->AcceleratorPressed(accelerator);

  WaitForClose();
  EXPECT_FALSE(OneClickSigninBubbleView::IsShowing());
  EXPECT_TRUE(on_start_sync_called_);
  EXPECT_EQ(OneClickSigninSyncStarter::UNDO_SYNC, mode_);
}
