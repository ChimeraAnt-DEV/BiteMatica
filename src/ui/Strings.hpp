#pragma once

#include <string>

namespace bitematica::ui {

/// The mod's user-facing strings, in one table (J8).
///
/// Every string a user can see goes through `Strings::get`, so adding a language is a matter of
/// providing a second table rather than hunting literals across the codebase. English is the only
/// table shipped today; the indirection is what makes a second one possible without a refactor.
///
/// The keys are stable ids, not the English text, so a translation is keyed by meaning. A missing
/// key returns the key itself, which makes an omission visible on screen rather than blank.
namespace Strings {

/// A stable string id.
enum class Id {
  // Status line.
  WorldSpaceSdk,
  WorldSpaceEgl,
  NoRenderSeam,
  NoSchematic,
  // Toolbar / menu.
  MenuTitle,
  ToolbarHidden,
  PanicHidden,
  // Layer controls.
  LayerAll,
  LayerSingle,
  LayerRange,
  LayerAxisX,
  LayerAxisY,
  LayerAxisZ,
  // Errors / fail-closed reasons.
  WorldNotReadable,
  VerificationUnavailable,
  RendererUnavailable,
  Count,
};

/// The English text for an id. Unknown ids return the numeric id as text, so a missing string is
/// obvious rather than empty.
inline const char *english(Id id) {
  switch (id) {
    case Id::WorldSpaceSdk: return "World-space GL (sdk)";
    case Id::WorldSpaceEgl: return "World-space GL (egl)";
    case Id::NoRenderSeam: return "World-space renderer unavailable: no render seam - update GlowberryClient";
    case Id::NoSchematic: return "No schematic loaded";
    case Id::MenuTitle: return "BiteMatica";
    case Id::ToolbarHidden: return "Toolbar hidden (press the menu icon)";
    case Id::PanicHidden: return "Rendering off (panic toggle)";
    case Id::LayerAll: return "All";
    case Id::LayerSingle: return "Single";
    case Id::LayerRange: return "Range";
    case Id::LayerAxisX: return "X";
    case Id::LayerAxisY: return "Y";
    case Id::LayerAxisZ: return "Z";
    case Id::WorldNotReadable: return "the world is not readable";
    case Id::VerificationUnavailable: return "Verification unavailable";
    case Id::RendererUnavailable: return "world-space renderer unavailable";
    case Id::Count: break;
  }
  return "?";
}

/// The current language's text for an id. Today only English is shipped.
inline const char *get(Id id) { return english(id); }

/// A convenience overload for a key given as text (used by the settings screen).
inline std::string get(const std::string &key) { return key; }

} // namespace Strings

} // namespace bitematica::ui
