// BiteMatica — the preloader glue.
//
// Everything format-, mesh- and UI-shaped lives in the `bitematica_core` static library
// so it can be unit-tested on a host (see test/host_tests.cpp). This file is the thin
// layer that talks to the preloader SDK: it registers the Mod Menu module, installs the
// touch and key callbacks, and translates the core's neutral `UiCommand`s into the
// preloader's `pl::modmenu::DrawCommand`s.
//
// Design notes that matter for this file specifically:
//
//  * There is no per-frame callback in the public SDK, and the mod must not invent one.
//    The overlay geometry is therefore submitted whenever it can change — on load, on a
//    Mod Menu toggle, and on every touch or key event that moves the view. Between those
//    events the submitted command list is static and the launcher redraws it each frame,
//    which is correct for a view that only changes when the player changes it.
//  * A schematic is loaded from the mod's `schematics/` folder, which the player fills by
//    copying files in. `<`/`>` cycle the folder and `Reload` re-scans it. Nothing here
//    needs a file picker, so the mod works the moment it is enabled.
//  * Input callbacks have no unregister API (documented in the SDK), so their bodies are
//    routed through the enabled flag rather than being removed.

#include <pl/Input.hpp>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "core/MaterialList.hpp"
#include "format/SchematicFormat.hpp"
#include "format/StructureWriter.hpp"
#include "mod/Config.hpp"
#include "mod/ControllerNavigator.hpp"
#include "mod/JsonConfig.hpp"
#include "mod/SchematicLibrary.hpp"
#include "render/HudOverlay.hpp"
#include "render/ViewCamera.hpp"

namespace {

using bitematica::core::Schematic;
using bitematica::mod::Config;
using bitematica::mod::ControllerNavigator;
using bitematica::render::HudOverlay;
using bitematica::render::UiCommand;
using bitematica::render::ViewCamera;

constexpr const char *kModuleId = "bitematica.overlay";
constexpr const char *kModuleName = "BiteMatica";

/// Draw-command colors the SDK expects are 0xAARRGGBB, matching `UiCommand`.
pl::modmenu::DrawCommandType toSdkKind(UiCommand::Kind kind) {
  switch (kind) {
    case UiCommand::Kind::Rect: return pl::modmenu::DrawCommandType::RectFilled;
    case UiCommand::Kind::Triangle: return pl::modmenu::DrawCommandType::TriangleFilled;
    case UiCommand::Kind::Text: return pl::modmenu::DrawCommandType::Text;
  }
  return pl::modmenu::DrawCommandType::RectFilled;
}

/// All mutable mod state. Kept in one object so the callbacks can reach it without
/// file-scope globals scattered across the translation unit.
class BiteMaticaMod {
public:
  static BiteMaticaMod &instance() {
    static BiteMaticaMod mod;
    return mod;
  }

  bool load();
  bool enable();
  bool disable();
  bool unload();

private:
  void registerMenuModule();
  void installInputCallbacks();
  void saveConfig();
  void scanLibrary();
  void loadCurrentSchematic();
  void refreshStatusLine();
  void submitFrame();
  bool performAction(bitematica::render::ToolbarButton::Action action);
  bool handleTouch(float x, float y);
  bool handleKey(int keyCode, bool isDown);
  void exportStructure();
  void setStatus(const std::string &message);

  ll::mod::NativeMod *mSelf{};
  Config mConfig;
  Schematic mSchematic;
  bool mHasSchematic{false};
  std::vector<std::string> mFiles;
  int mFileIndex{0};

  ViewCamera mCamera;
  std::vector<bitematica::core::MaterialEntry> mMaterials;
  std::vector<bitematica::render::ToolbarButton> mButtons;
  ControllerNavigator mNavigator;
  std::string mStatus;
  /// Transient controller focus hint, appended to the info line. Cleared on action.
  std::string mHint;
  bool mEnabled{false};
};



// --- lifecycle ----------------------------------------------------------------------

bool BiteMaticaMod::load() {
  mSelf = ll::mod::NativeMod::current();
  if (mSelf == nullptr) return false;
  auto &log = mSelf->getLogger();

  std::error_code ec;
  std::filesystem::create_directories(mSelf->getConfigDir(), ec);
  std::filesystem::create_directories(mSelf->getDataDir() / "exports", ec);

  const std::filesystem::path configPath = mSelf->getConfigDir() / "config.json";
  std::string text;
  std::ifstream in(configPath);
  if (in) {
    text.assign((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
  }
  std::string error;
  if (!text.empty() && !bitematica::mod::JsonConfig::parse(text, mConfig, error)) {
    log.warn("config could not be read ({}); using defaults", error);
    mConfig = Config{};
  }
  mConfig.normalize();

  scanLibrary();
  if (!mFiles.empty()) {
    mFileIndex = 0;
    loadCurrentSchematic();
  } else {
    setStatus("No schematic found - add one to the mod's schematics folder");
  }
  log.info("BiteMatica loaded; {} schematic(s) found", mFiles.size());
  return true;
}

bool BiteMaticaMod::enable() {
  mEnabled = true;
  registerMenuModule();
  installInputCallbacks();
  refreshStatusLine();
  submitFrame();
  return true;
}

bool BiteMaticaMod::disable() {
  mEnabled = false;
  pl::modmenu::unregisterModule(kModuleId);
  if (mSelf != nullptr) mSelf->getLogger().info("BiteMatica disabled");
  return true;
}

bool BiteMaticaMod::unload() {
  mEnabled = false;
  return true;
}

// --- Mod Menu ------------------------------------------------------------------------

void BiteMaticaMod::registerMenuModule() {
  pl::modmenu::ModuleBuilder builder(kModuleId, kModuleName);
  builder.modId(mSelf != nullptr ? mSelf->getId() : "bitematica")
      .description("Load, view and export client-side schematics.")
      .defaultEnabled(mConfig.enabled)
      .onToggle([this](std::string_view moduleId, bool enabled) {
        (void)moduleId;
        mConfig.enabled = enabled;
        if (!enabled) {
          // Clear the overlay so a disabled module draws nothing at all.
          const std::vector<pl::modmenu::DrawCommand> empty;
          pl::modmenu::submitDrawCommands(kModuleId, empty);
        } else {
          submitFrame();
        }
        saveConfig();
      })
      .onConfigChanged([this](std::string_view, std::string_view key,
                              std::string_view value) {
        const std::string k(key);
        const std::string v(value);
        if (k == "opacity") {
          mConfig.opacity = std::atoi(v.c_str());
        } else if (k == "renderMode") {
          mConfig.renderMode = static_cast<bitematica::mod::RenderMode>(std::atoi(v.c_str()));
        } else if (k == "showMaterialList") {
          mConfig.showMaterialList = v == "true";
        } else if (k == "ghostPlacement") {
          mConfig.ghostPlacement = v == "true";
        }
        mConfig.normalize();
        saveConfig();
        submitFrame();
      })
      .config("renderMode", "Render Mode", pl::modmenu::ConfigType::Radio,
              std::to_string(static_cast<int>(mConfig.renderMode)), "Overlay,Skeleton,Hidden")
      .config("opacity", "Opacity", pl::modmenu::ConfigType::SliderInt,
              std::to_string(mConfig.opacity), "10", "100")
      .config("showMaterialList", "Material List", pl::modmenu::ConfigType::Toggle,
              mConfig.showMaterialList ? "true" : "false")
      .config("ghostPlacement", "Ghost Placement", pl::modmenu::ConfigType::Toggle,
              mConfig.ghostPlacement ? "true" : "false");
  if (!builder.registerModule() && mSelf != nullptr) {
    mSelf->getLogger().warn("BiteMatica: the Mod Menu module could not be registered");
  }
}

// --- input ---------------------------------------------------------------------------

void BiteMaticaMod::installInputCallbacks() {
  pl::input::registerTouchCallback([this](const pl::input::TouchEvent &event) {
    if (!mEnabled) return false;
    if (event.action != 0 /* ACTION_DOWN */) return false;
    return handleTouch(event.x, event.y);
  });

  pl::input::registerKeyCallback([this](const pl::input::KeyEvent &event) {
    if (!mEnabled) return false;
    return handleKey(event.keyCode, event.isKeyDown);
  });
}

bool BiteMaticaMod::handleTouch(float x, float y) {
  const auto *hit = HudOverlay::hitTest(mButtons, x, y);
  if (hit == nullptr) return false;
  mHint.clear();
  performAction(hit->action);
  submitFrame();
  return true; // consume the tap so it does not reach the game world
}

bool BiteMaticaMod::handleKey(int keyCode, bool isDown) {
  // The controller navigator owns focus; it reports back an action to run.
  const ControllerNavigator::Result result =
      mNavigator.handleKey(keyCode, isDown, static_cast<int>(mButtons.size()));
  if (!result.handled) return false;

  if (result.activated) {
    mHint.clear();
    performAction(result.action);
  } else if (mNavigator.engaged()) {
    const int focus = mNavigator.resolvedFocus(static_cast<int>(mButtons.size()));
    if (focus >= 0 && focus < static_cast<int>(mButtons.size())) {
      mHint = std::string("focus: ") +
              ControllerNavigator::actionName(mButtons[static_cast<size_t>(focus)].action);
    }
  }
  submitFrame();
  return true;
}

// --- actions -------------------------------------------------------------------------

bool BiteMaticaMod::performAction(bitematica::render::ToolbarButton::Action action) {
  using Action = bitematica::render::ToolbarButton::Action;
  switch (action) {
    case Action::PreviousFile:
      if (!mFiles.empty()) {
        mFileIndex = bitematica::mod::SchematicLibrary::step(
            mFileIndex, -1, static_cast<int>(mFiles.size()));
        loadCurrentSchematic();
      }
      return true;
    case Action::NextFile:
      if (!mFiles.empty()) {
        mFileIndex = bitematica::mod::SchematicLibrary::step(
            mFileIndex, 1, static_cast<int>(mFiles.size()));
        loadCurrentSchematic();
      }
      return true;
    case Action::CycleMode: {
      int mode = static_cast<int>(mConfig.renderMode);
      mode = (mode + 1) % 3;
      mConfig.renderMode = static_cast<bitematica::mod::RenderMode>(mode);
      mConfig.normalize();
      saveConfig();
      return true;
    }
    case Action::ToggleMaterials:
      mConfig.showMaterialList = !mConfig.showMaterialList;
      saveConfig();
      return true;
    case Action::RotateLeft:
      mCamera.yawDegrees -= 15.0f;
      refreshStatusLine();
      return true;
    case Action::RotateRight:
      mCamera.yawDegrees += 15.0f;
      refreshStatusLine();
      return true;
    case Action::ZoomIn:
      mCamera.scale = std::min(mCamera.scale * 1.15f, 96.0f);
      refreshStatusLine();
      return true;
    case Action::ZoomOut:
      mCamera.scale = std::max(mCamera.scale / 1.15f, 4.0f);
      refreshStatusLine();
      return true;
    case Action::NudgeUp:
      mCamera.panY += 1.0f;
      return true;
    case Action::NudgeDown:
      mCamera.panY -= 1.0f;
      return true;
    case Action::ToggleGhost:
      mConfig.ghostPlacement = !mConfig.ghostPlacement;
      saveConfig();
      return true;
    case Action::ExportStructure:
      exportStructure();
      return true;
    case Action::Reload:
      scanLibrary();
      if (!mFiles.empty()) {
        if (mFileIndex >= static_cast<int>(mFiles.size())) mFileIndex = 0;
        loadCurrentSchematic();
      } else {
        mHasSchematic = false;
        setStatus("No schematic found");
      }
      return true;
  }
  return false;
}

// --- loading -------------------------------------------------------------------------

void BiteMaticaMod::scanLibrary() {
  const std::filesystem::path dir = mSelf->getModDir() / "schematics";
  mFiles = bitematica::mod::SchematicLibrary::scan(dir.string());
  if (mFileIndex >= static_cast<int>(mFiles.size())) mFileIndex = 0;
}

void BiteMaticaMod::loadCurrentSchematic() {
  if (mFiles.empty() || mFileIndex < 0 ||
      mFileIndex >= static_cast<int>(mFiles.size())) {
    mHasSchematic = false;
    return;
  }
  const std::string path = mFiles[static_cast<size_t>(mFileIndex)];
  const bitematica::format::LoadResult result =
      bitematica::format::SchematicFormat::readPath(path);
  if (!result.success) {
    mHasSchematic = false;
    setStatus("Load failed: " + result.error);
    if (mSelf != nullptr) mSelf->getLogger().warn("Failed to load {}: {}", path, result.error);
    return;
  }
  mSchematic = result.schematic;
  mHasSchematic = true;
  mMaterials = bitematica::core::MaterialList::build(mSchematic);
  mConfig.lastLoadedFile = path;
  // Re-center and pick a scale that fits the build on a phone screen.
  mCamera = ViewCamera{};
  const float span = static_cast<float>(std::max({mSchematic.sizeX(), mSchematic.sizeY(),
                                                  mSchematic.sizeZ()}));
  mCamera.scale = std::max(4.0f, std::min(28.0f, 520.0f / std::max(1.0f, span)));
  saveConfig();
  refreshStatusLine();
}

void BiteMaticaMod::exportStructure() {
  if (!mHasSchematic) {
    setStatus("Nothing loaded to export");
    return;
  }
  const std::filesystem::path exports = mSelf->getDataDir() / "exports";
  std::error_code ec;
  std::filesystem::create_directories(exports, ec);
  std::string base = std::filesystem::path(mSchematic.name).filename().string();
  if (base.empty()) base = "bitematica_export";
  const std::filesystem::path outPath = exports / (base + ".mcstructure");
  std::string error;
  if (bitematica::format::StructureWriter::write(mSchematic, outPath.string(), error)) {
    setStatus("Exported to " + outPath.string());
  } else {
    setStatus("Export failed: " + error);
  }
}

// --- overlay -------------------------------------------------------------------------

void BiteMaticaMod::refreshStatusLine() {
  if (!mHasSchematic) {
    if (mStatus.empty()) setStatus("No schematic loaded");
    return;
  }
  char buffer[160];
  const char *mode = mConfig.renderMode == bitematica::mod::RenderMode::Skeleton
                         ? "skeleton"
                         : (mConfig.renderMode == bitematica::mod::RenderMode::Hidden
                                ? "hidden"
                                : "overlay");
  std::snprintf(buffer, sizeof(buffer), "%s  %dx%dx%d  %s  yaw %.0f", mSchematic.name.c_str(),
                mSchematic.sizeX(), mSchematic.sizeY(), mSchematic.sizeZ(), mode,
                mCamera.yawDegrees);
  setStatus(buffer);
}

void BiteMaticaMod::setStatus(const std::string &message) { mStatus = message; }

void BiteMaticaMod::submitFrame() {
  mButtons = HudOverlay::layoutToolbar(mConfig);

  // Center the view on the actual HUD surface so the build sits on screen.
  const pl::modmenu::HudSurfaceSize surface = pl::modmenu::getHudSurfaceSize();
  mCamera.centerX = surface.width > 0.0f ? surface.width * 0.5f : 640.0f;
  mCamera.centerY = surface.height > 0.0f ? surface.height * 0.52f : 360.0f;

  const std::string infoLine = mHint.empty() ? mStatus : (mStatus + "   " + mHint);
  const std::vector<UiCommand> commands = HudOverlay::buildCommands(
      mSchematic, mConfig, mCamera, mButtons, mMaterials, infoLine, mHasSchematic);

  std::vector<pl::modmenu::DrawCommand> sdkCommands;
  sdkCommands.reserve(commands.size());
  for (const UiCommand &command : commands) {
    pl::modmenu::DrawCommand out{};
    out.type = toSdkKind(command.kind);
    out.x = command.x;
    out.y = command.y;
    out.w = command.w;
    out.h = command.h;
    out.x3 = command.x3;
    out.y3 = command.y3;
    out.color = command.color;
    out.size = command.size;
    out.text = command.text;
    sdkCommands.push_back(std::move(out));
  }
  pl::modmenu::submitDrawCommands(kModuleId, sdkCommands);
}

void BiteMaticaMod::saveConfig() {
  if (mSelf == nullptr) return;
  std::ofstream out(mSelf->getConfigDir() / "config.json", std::ios::trunc);
  if (out) out << bitematica::mod::JsonConfig::serialize(mConfig);
}

} // namespace

PL_REGISTER_MOD(BiteMaticaMod, BiteMaticaMod::instance())
