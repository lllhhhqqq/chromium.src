// Copyright (c) 2013 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef ASH_WM_DOCK_DOCKED_WINDOW_LAYOUT_MANAGER_H_
#define ASH_WM_DOCK_DOCKED_WINDOW_LAYOUT_MANAGER_H_

#include <memory>

#include "ash/ash_export.h"
#include "ash/shelf/shelf_layout_manager_observer.h"
#include "ash/shell_observer.h"
#include "ash/snap_to_pixel_layout_manager.h"
#include "ash/wm/dock/dock_types.h"
#include "ash/wm/dock/docked_window_layout_manager_observer.h"
#include "ash/wm/window_state_observer.h"
#include "base/compiler_specific.h"
#include "base/macros.h"
#include "base/observer_list.h"
#include "base/time/time.h"
#include "ui/aura/window.h"
#include "ui/aura/window_observer.h"
#include "ui/gfx/geometry/rect.h"
#include "ui/keyboard/keyboard_controller_observer.h"
#include "ui/wm/public/activation_change_observer.h"

namespace aura {
class Window;
}

namespace gfx {
class Point;
}

namespace views {
class Widget;
}

namespace ash {
class DockedBackgroundWidget;
class DockedWindowLayoutManagerObserver;
class DockedWindowResizerTest;
class Shelf;
class ShelfLayoutManager;
class WorkspaceController;

struct WindowWithHeight {
  explicit WindowWithHeight(aura::Window* window) :
    window_(window),
    height_(window->bounds().height()) { }
  aura::Window* window() { return window_; }
  const aura::Window* window() const { return window_; }
  aura::Window* window_;
  int height_;
};

// DockedWindowLayoutManager is responsible for organizing windows when they are
// docked to the side of a screen. It is associated with a specific container
// window (i.e. kShellWindowId_DockContainer) and controls the layout of any
// windows added to that container.
//
// The constructor takes a |dock_container| argument which is expected to set
// its layout manager to this instance, e.g.:
// dock_container->SetLayoutManager(
//     new DockedWindowLayoutManager(dock_container));
//
// TODO(varkha): extend BaseLayoutManager instead of LayoutManager to inherit
// common functionality.
class ASH_EXPORT DockedWindowLayoutManager
    : public SnapToPixelLayoutManager,
      public ash::ShellObserver,
      public aura::WindowObserver,
      public aura::client::ActivationChangeObserver,
      public keyboard::KeyboardControllerObserver,
      public ShelfLayoutManagerObserver,
      public wm::WindowStateObserver {
 public:
  // Maximum width of the docked windows area.
  static const int kMaxDockWidth;

  // Minimum width of the docked windows area.
  static const int kMinDockWidth;

  DockedWindowLayoutManager(aura::Window* dock_container,
                            WorkspaceController* workspace_controller);
  ~DockedWindowLayoutManager() override;

  // Disconnects observers before container windows get destroyed.
  void Shutdown();

  // Management of the observer list.
  virtual void AddObserver(DockedWindowLayoutManagerObserver* observer);
  virtual void RemoveObserver(DockedWindowLayoutManagerObserver* observer);

  // Called by a DockedWindowResizer to update which window is being dragged.
  // Starts observing the window unless it is a child.
  void StartDragging(aura::Window* window);

  // Called by a DockedWindowResizer when a dragged window is docked.
  void DockDraggedWindow(aura::Window* window);

  // Called by a DockedWindowResizer when a dragged window is no longer docked.
  void UndockDraggedWindow();

  // Called by a DockedWindowResizer when a window is no longer being dragged.
  // Stops observing the window unless it is a child.
  // Records |action| by |source| in UMA.
  void FinishDragging(DockedAction action, DockedActionSource source);

  // Checks the rules and possibly updates the docked layout to match
  // the |alignment|. May not apply the |alignment| when
  // the current shelf alignment conflicts. Never clears the |alignment_|.
  void MaybeSetDesiredDockedAlignment(DockedAlignment alignment);

  Shelf* shelf() { return shelf_; }
  void SetShelf(Shelf* shelf);

  // Calculates if a window is touching the screen edges and returns edge.
  DockedAlignment GetAlignmentOfWindow(const aura::Window* window) const;

  // Used to snap docked windows to the side of screen during drag.
  DockedAlignment CalculateAlignment() const;

  void set_preferred_alignment(DockedAlignment preferred_alignment) {
    preferred_alignment_ = preferred_alignment;
  }

  void set_event_source(DockedActionSource event_source) {
    event_source_ = event_source;
  }

  // Returns true when a window can be docked. Windows cannot be docked at the
  // edge used by the shelf or the edge opposite from existing dock.
  bool CanDockWindow(aura::Window* window, DockedAlignment desired_alignment);

  aura::Window* dock_container() const { return dock_container_; }

  // Returns current bounding rectangle of docked windows area.
  const gfx::Rect& docked_bounds() const { return docked_bounds_; }

  // Returns last known coordinates of |dragged_window_| after Relayout.
  const gfx::Rect dragged_bounds() const { return dragged_bounds_;}

  // Returns true if currently dragged window is docked at the screen edge.
  bool is_dragged_window_docked() const { return is_dragged_window_docked_; }

  // Updates docked layout when shelf bounds change.
  void OnShelfBoundsChanged();

  // SnapLayoutManager:
  void OnWindowResized() override;
  void OnWindowAddedToLayout(aura::Window* child) override;
  void OnWillRemoveWindowFromLayout(aura::Window* child) override {}
  void OnWindowRemovedFromLayout(aura::Window* child) override;
  void OnChildWindowVisibilityChanged(aura::Window* child,
                                      bool visibile) override;
  void SetChildBounds(aura::Window* child,
                      const gfx::Rect& requested_bounds) override;

  // ash::ShellObserver:
  void OnDisplayWorkAreaInsetsChanged() override;
  void OnFullscreenStateChanged(bool is_fullscreen,
                                aura::Window* root_window) override;
  void OnShelfAlignmentChanged(aura::Window* root_window) override;

  // ShelfLayoutManagerObserver:
  void OnBackgroundUpdated(ShelfBackgroundType background_type,
                           BackgroundAnimatorChangeType change_type) override;

  // wm::WindowStateObserver:
  void OnPreWindowStateTypeChange(wm::WindowState* window_state,
                                  wm::WindowStateType old_type) override;

  // aura::WindowObserver:
  void OnWindowBoundsChanged(aura::Window* window,
                             const gfx::Rect& old_bounds,
                             const gfx::Rect& new_bounds) override;
  void OnWindowVisibilityChanging(aura::Window* window, bool visible) override;
  void OnWindowDestroying(aura::Window* window) override;

  // aura::client::ActivationChangeObserver:
  void OnWindowActivated(
      aura::client::ActivationChangeObserver::ActivationReason reason,
      aura::Window* gained_active,
      aura::Window* lost_active) override;

 private:
  class ShelfWindowObserver;
  friend class DockedWindowLayoutManagerTest;
  friend class DockedWindowResizerTest;

  // Width of the gap between the docked windows and a workspace.
  static const int kMinDockGap;

  // Ideal (starting) width of the dock.
  static const int kIdealWidth;

  // Returns the alignment of the docked windows other than the |child|.
  DockedAlignment CalculateAlignmentExcept(const aura::Window* child) const;

  // Determines if the |alignment| is applicable taking into account
  // the shelf alignment.
  bool IsDockedAlignmentValid(DockedAlignment alignment) const;

  // Keep at most kMaxVisibleWindows visible in the dock and minimize the rest
  // (except for |child|).
  void MaybeMinimizeChildrenExcept(aura::Window* child);

  // Minimize / restore window and relayout.
  void MinimizeDockedWindow(wm::WindowState* window_state);
  void RestoreDockedWindow(wm::WindowState* window_state);

  // Record user-initiated |action| by |source| in UMA metrics.
  void RecordUmaAction(DockedAction action, DockedActionSource source);

  // Updates |docked_width_| and UMA histograms.
  void UpdateDockedWidth(int width);

  // Updates docked layout state when a window gets inside the dock.
  void OnDraggedWindowDocked(aura::Window* window);

  // Updates docked layout state when a window gets outside the dock.
  void OnDraggedWindowUndocked();

  // Returns true if there are any windows currently docked.
  bool IsAnyWindowDocked();

  // Returns DOCKED_ALIGNMENT_LEFT if the |window|'s left edge is closer to
  // the |dock_container_|'s left edge than the |window|'s right edge to
  // the |dock_container_|'s right edge. Returns DOCKED_ALIGNMENT_RIGHT
  // otherwise.
  DockedAlignment GetEdgeNearestWindow(const aura::Window* window) const;

  // Called whenever the window layout might change.
  void Relayout();

  // Calculates target heights (and fills it in |visible_windows| array) such
  // that the vertical space is fairly distributed among the windows taking
  // into account their minimum and maximum size. Returns free vertical space
  // (positive value) that remains after resizing all windows or deficit
  // (negative value) if not all the windows fit.
  int CalculateWindowHeightsAndRemainingRoom(
      const gfx::Rect work_area,
      std::vector<WindowWithHeight>* visible_windows);

  // Calculate ideal width for the docked area. It will get used to adjust the
  // dragged window or other windows as necessary.
  int CalculateIdealWidth(const std::vector<WindowWithHeight>& visible_windows);

  // Fan out windows evenly distributing the overlap or remaining free space.
  // Adjust the widths of the windows trying to make them all same. If this
  // is not possible, center the windows in the docked area.
  void FanOutChildren(const gfx::Rect& work_area,
                      int ideal_docked_width,
                      int available_room,
                      std::vector<WindowWithHeight>* visible_windows);

  // Updates |docked_bounds_| and workspace insets when bounds of docked windows
  // area change. Passing |reason| to observers allows selectively skipping
  // notifications.
  void UpdateDockBounds(DockedWindowLayoutManagerObserver::Reason reason);

  // Called whenever the window stacking order needs to be updated (e.g. focus
  // changes or a window is moved).
  void UpdateStacking(aura::Window* active_window);

  // keyboard::KeyboardControllerObserver:
  void OnKeyboardBoundsChanging(const gfx::Rect& keyboard_bounds) override;

  // Parent window associated with this layout manager.
  aura::Window* dock_container_;
  // Protect against recursive calls to Relayout().
  bool in_layout_;

  // A window that is being dragged (whether docked or not).
  // Windows are tracked by docked layout manager only if they are docked;
  // however we need to know if a window is being dragged in order to avoid
  // positioning it or even considering it for layout.
  aura::Window* dragged_window_;

  // True if the window being dragged is currently docked.
  bool is_dragged_window_docked_;

  // Previously docked windows use a more relaxed dragging sorting algorithm
  // that uses assumption that a window starts being dragged out of position
  // that was previously established in Relayout. This allows easier reordering.
  bool is_dragged_from_dock_;

  // The shelf to respond to alignment changes.
  Shelf* shelf_;

  // Workspace controller that can be checked for fullscreen mode.
  WorkspaceController* workspace_controller_;
  // Tracks if any window in the same root window is in fullscreen mode.
  bool in_fullscreen_;
  // Current width of the dock.
  int docked_width_;

  // Last bounds that were sent to observers.
  gfx::Rect docked_bounds_;

  // Target bounds of a docked window being dragged.
  gfx::Rect dragged_bounds_;

  // Side of the screen that the dock is positioned at.
  DockedAlignment alignment_;

  // The preferred alignment of the next window to be added to docked layout.
  DockedAlignment preferred_alignment_;

  // The current event source
  DockedActionSource event_source_;

  // The last active window. Used to maintain stacking order even if no windows
  // are currently focused.
  aura::Window* last_active_window_;

  // Timestamp of the last user-initiated action that changed docked state.
  // Used in UMA metrics.
  base::Time last_action_time_;

  // Observes shelf for bounds changes.
  std::unique_ptr<ShelfWindowObserver> shelf_observer_;

  // Widget used to paint a background for the docked area.
  std::unique_ptr<DockedBackgroundWidget> background_widget_;

  // Observers of dock bounds changes.
  base::ObserverList<DockedWindowLayoutManagerObserver> observer_list_;

  DISALLOW_COPY_AND_ASSIGN(DockedWindowLayoutManager);
};

}  // namespace ash

#endif  // ASH_WM_DOCK_DOCKED_WINDOW_LAYOUT_MANAGER_H_
