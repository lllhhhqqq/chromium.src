// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef UI_VIEWS_CONTROLS_COMBOBOX_COMBOBOX_H_
#define UI_VIEWS_CONTROLS_COMBOBOX_COMBOBOX_H_

#include "base/macros.h"
#include "base/memory/weak_ptr.h"
#include "base/strings/string16.h"
#include "base/time/time.h"
#include "ui/views/controls/button/button.h"
#include "ui/views/controls/prefix_delegate.h"

namespace gfx {
class FontList;
}

namespace ui {
class ComboboxModel;
class MenuModel;
}

namespace views {
namespace test {
class ComboboxTestApi;
}

class ComboboxListener;
class CustomButton;
class MenuRunner;
class Painter;
class PrefixSelector;

// A non-editable combobox (aka a drop-down list or selector).
// Combobox has two distinct parts, the drop down arrow and the text. Combobox
// offers two distinct behaviors:
// * STYLE_NORMAL: typical combobox, clicking on the text and/or button shows
// the drop down, arrow keys change selection, selected index can be changed by
// the user to something other than the first item.
// * STYLE_ACTION: clicking on the text notifies the listener. The menu can be
// shown only by clicking on the arrow. The selected index is always reverted to
// 0 after the listener is notified.
class VIEWS_EXPORT Combobox : public PrefixDelegate, public ButtonListener {
 public:
  // The style of the combobox.
  enum Style {
    STYLE_NORMAL,
    STYLE_ACTION,
  };

  // The combobox's class name.
  static const char kViewClassName[];

  // |model| is not owned by the combobox.
  explicit Combobox(ui::ComboboxModel* model);
  ~Combobox() override;

  static const gfx::FontList& GetFontList();

  // Sets the listener which will be called when a selection has been made.
  void set_listener(ComboboxListener* listener) { listener_ = listener; }

  void SetStyle(Style style);

  // Informs the combobox that its model changed.
  void ModelChanged();

  // Gets/Sets the selected index.
  int selected_index() const { return selected_index_; }
  void SetSelectedIndex(int index);

  // Looks for the first occurrence of |value| in |model()|. If found, selects
  // the found index and returns true. Otherwise simply noops and returns false.
  bool SelectValue(const base::string16& value);

  ui::ComboboxModel* model() const { return model_; }

  // Set the accessible name of the combobox.
  void SetAccessibleName(const base::string16& name);

  // Visually marks the combobox as having an invalid value selected.
  // When invalid, it paints with white text on a red background.
  // Callers are responsible for restoring validity with selection changes.
  void SetInvalid(bool invalid);
  bool invalid() const { return invalid_; }

  // Returns the width of the arrow button component of the combobox: the arrow
  // button itself, and the padding on either side of it.
  int GetArrowButtonWidth() const;

  // Overridden from View:
  gfx::Size GetPreferredSize() const override;
  const char* GetClassName() const override;
  bool SkipDefaultKeyEventProcessing(const ui::KeyEvent& e) override;
  bool OnKeyPressed(const ui::KeyEvent& e) override;
  bool OnKeyReleased(const ui::KeyEvent& e) override;
  void OnPaint(gfx::Canvas* canvas) override;
  void OnFocus() override;
  void OnBlur() override;
  void GetAccessibleState(ui::AXViewState* state) override;
  void Layout() override;

  // Overridden from PrefixDelegate:
  int GetRowCount() override;
  int GetSelectedRow() override;
  void SetSelectedRow(int row) override;
  base::string16 GetTextForRow(int row) override;

  // Overriden from ButtonListener:
  void ButtonPressed(Button* sender, const ui::Event& event) override;

 private:
  friend class test::ComboboxTestApi;

  class ComboboxMenuModelAdapter;

  // Updates the border according to the current state.
  void UpdateBorder();

  // Given bounds within our View, this helper mirrors the bounds if necessary.
  void AdjustBoundsForRTLUI(gfx::Rect* rect) const;

  // Draws the selected value of the drop down list
  void PaintText(gfx::Canvas* canvas);

  // Draws the button images.
  void PaintButtons(gfx::Canvas* canvas);

  // Show the drop down list
  void ShowDropDownMenu(ui::MenuSourceType source_type);

  // Called when the selection is changed by the user.
  void OnPerformAction();

  int GetDisclosureArrowLeftPadding() const;
  int GetDisclosureArrowRightPadding() const;

  // Returns the size of the disclosure arrow.
  gfx::Size ArrowSize() const;

  // Finds the size of the largest menu label or, for STYLE_ACTION, the size of
  // the selected label.
  gfx::Size GetContentSize() const;

  // Handles the clicking event.
  void HandleClickEvent();

  PrefixSelector* GetPrefixSelector();

  // Our model. Not owned.
  ui::ComboboxModel* model_;

  // The visual style of this combobox.
  Style style_;

  // Our listener. Not owned. Notified when the selected index change.
  ComboboxListener* listener_;

  // The current selected index; -1 and means no selection.
  int selected_index_;

  // True when the selection is visually denoted as invalid.
  bool invalid_;

  // The accessible name of this combobox.
  base::string16 accessible_name_;

  // A helper used to select entries by keyboard input.
  scoped_ptr<PrefixSelector> selector_;

  // Adapts a ComboboxModel for use by a views MenuRunner.
  scoped_ptr<ui::MenuModel> menu_model_adapter_;

  // Like MenuButton, we use a time object in order to keep track of when the
  // combobox was closed. The time is used for simulating menu behavior; that
  // is, if the menu is shown and the button is pressed, we need to close the
  // menu. There is no clean way to get the second click event because the
  // menu is displayed using a modal loop and, unlike regular menus in Windows,
  // the button is not part of the displayed menu.
  base::Time closed_time_;

  // The maximum dimensions of the content in the dropdown.
  gfx::Size content_size_;

  // The painters or images that are used when |style_| is STYLE_BUTTONS. The
  // first index means the state of unfocused or focused.
  // The images are owned by ResourceBundle.
  scoped_ptr<Painter> body_button_painters_[2][Button::STATE_COUNT];
  std::vector<const gfx::ImageSkia*>
      menu_button_images_[2][Button::STATE_COUNT];

  // The transparent buttons to handle events and render buttons. These are
  // placed on top of this combobox as child views, accept event and manage the
  // button states. These are not rendered but when |style_| is
  // STYLE_NOTIFY_ON_CLICK, a Combobox renders the button images according to
  // these button states.
  // The base View takes the ownerships of these as child views.
  CustomButton* text_button_;
  CustomButton* arrow_button_;

  // Set while the dropdown is showing. Ensures the menu is closed if |this| is
  // destroyed.
  scoped_ptr<views::MenuRunner> menu_runner_;

  // Used for making calbacks.
  base::WeakPtrFactory<Combobox> weak_ptr_factory_;

  DISALLOW_COPY_AND_ASSIGN(Combobox);
};

}  // namespace views

#endif  // UI_VIEWS_CONTROLS_COMBOBOX_COMBOBOX_H_
