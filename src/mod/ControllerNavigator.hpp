#pragma once

#include <string>
#include <vector>

#include "render/HudOverlay.hpp"

namespace bitematica::mod {

/// Turns gamepad input into toolbar navigation.
///
/// GlowberryClient delivers controller buttons as Android key events, so the whole
/// controller path is "map a key code to a focus move or an activation". The navigator
/// owns the focus index and exposes the focused button so the overlay can outline it;
/// it never reads the buttons itself beyond the count, which keeps touch and controller
/// using the same `ToolbarButton` list.
///
/// Focus moves on a 6-column grid, matching `HudOverlay::layoutToolbar`. `kColumns` is
/// duplicated here on purpose: the navigator must not depend on the layout function's
/// internals, and a mismatch is caught by `ControllerNavigatorTest`.
class ControllerNavigator {
public:
  static constexpr int kColumns = 6;
  static constexpr int kRowCount = 2;

  /// Android key codes the navigator understands.
  enum Key {
    DpadUp = 19,
    DpadDown = 20,
    DpadLeft = 21,
    DpadRight = 22,
    DpadCenter = 23,
    ButtonA = 96,
    ButtonB = 97,
    ButtonX = 99,
    ButtonY = 100,
    ButtonL1 = 102,
    ButtonR1 = 103,
    ButtonStart = 108,
  };

  /// What a key press resolved to.
  struct Result {
    bool handled{};
    bool activated{};
    /// Set only when `activated` is true.
    render::ToolbarButton::Action action{render::ToolbarButton::Action::CycleMode};
    /// A short label for the on-screen focus hint, e.g. "Materials".
    std::string label;
  };

  int focusIndex() const { return mFocusIndex; }

  /// True while the controller has claimed navigation (any d-pad press so far).
  bool engaged() const { return mEngaged; }

  void release() { mEngaged = false; }

  /// Handles a key event. `count` is the number of toolbar buttons on screen.
  Result handleKey(int keyCode, bool isKeyDown, int count);

  /// The focused button index clamped to a live toolbar, or -1 when empty.
  int resolvedFocus(int count) const {
    if (count <= 0) return -1;
    if (mFocusIndex < 0) return 0;
    if (mFocusIndex >= count) return count - 1;
    return mFocusIndex;
  }

  /// Human-readable label for a toolbar action, also used by the focus hint.
  static const char *actionName(render::ToolbarButton::Action action);

  /// The toolbar action at a grid index; must match `HudOverlay`'s button order.
  static render::ToolbarButton::Action actionForIndex(int index);

private:
  int mFocusIndex{0};
  bool mEngaged{false};
};

} // namespace bitematica::mod
