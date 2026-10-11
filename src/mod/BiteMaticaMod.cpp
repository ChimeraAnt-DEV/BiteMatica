// BiteMatica — the preloader glue.
//
// Everything format-, mesh-, world- and UI-shaped lives in the `bitematica_core` static library so
// it can be unit-tested on a host (see test/host_tests*.cpp). This file is the thin layer that talks
// to the preloader SDK:
//
//  * It registers the Mod Menu module and installs touch/key callbacks (as before).
//  * It registers a per-frame render callback through the SDK's new `pl/Render.hpp` API, which runs
//    on the render thread with the GL context current, and draws the schematic as world-space
//    geometry with the camera matrices the SDK resolves.
//  * It draws the 2D toolbar/material overlay through the Mod Menu HUD commands, unchanged.
//
// The two drawing paths are deliberately separate: world-space geometry is GL (depth-tested, hidden
// by terrain), the toolbar is HUD commands (always on top). Mixing them would be wrong — a toolbar
// that terrain occludes is not a toolbar.
//
// Meshing is done off the render thread: a worker builds section meshes and hands them back, and the
// render callback only uploads buffers that are ready and draws them. The render thread never meshes.

#include <pl/Input.hpp>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/Render.hpp>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "core/LiveMaterials.hpp"
#include "core/MaterialList.hpp"
#include "format/LitematicWriter.hpp"
#include "format/SchematicFormat.hpp"
#include "format/StructureWriter.hpp"
#include "gles/SchematicRenderer.hpp"
#include "mesh/SectionMesher.hpp"
#include "mod/Config.hpp"
#include "mod/ControllerNavigator.hpp"
#include "mod/GameWorldSource.hpp"
#include "mod/JsonConfig.hpp"
#include "mod/SchematicLibrary.hpp"
#include "render/HudOverlay.hpp"
#include "render/MathTypes.hpp"
#include "render/ViewCamera.hpp"
#include "world/CheatPlacement.hpp"
#include "world/Placement.hpp"
#include "world/Selection.hpp"
#include "world/Verifier.hpp"

namespace {

using bitematica::core::Schematic;
using bitematica::core::BlockPos;
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

/// Converts the mod's own column-major matrix to the SDK's, at the single boundary where the two
/// representations meet. They are both column-major, so this is a straight copy.
pl::render::Mat4 toSdkMatrix(const bitematica::render::Mat4 &m) {
  pl::render::Mat4 out;
  for (int i = 0; i < 16; ++i) out.m[i] = m.m[i];
  return out;
}

/// The mesh a worker produced, handed to the render thread through a mutex-guarded slot.
struct PendingMesh {
  std::vector<bitematica::mesh::SectionMesh> sections;
  bool ready{false};
  long long emittedQuads{};
  bool truncated{};
};

/// All mutable mod state. Kept in one object so the callbacks can reach it without file-scope
/// globals scattered across the translation unit.
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
  void installRenderCallback();
  void saveConfig();
  void scanLibrary();
  void loadCurrentSchematic();
  void refreshStatusLine();
  void submitHud();
  void rebuildMeshAsync();
  void uploadPendingMesh();
  void drawWorldSpace(const pl::render::RenderContext &context);
  bool performAction(bitematica::render::ToolbarButton::Action action);
  bool handleTouch(float x, float y);
  bool handleKey(int keyCode, bool isDown);
  void exportStructure();
  void exportCsv();
  void saveLitematic();
  void runVerification();
  void moveToPlayer();
  void setStatus(const std::string &message);
  void currentLayerRange(int &outMin, int &outMax) const;

  ll::mod::NativeMod *mSelf{};
  Config mConfig;
  Schematic mSchematic;
  bool mHasSchematic{false};
  std::vector<std::string> mFiles;
  int mFileIndex{0};

  /// Where the schematic sits in the world. Set to the player's position on load/move.
  bitematica::world::Placement mPlacement;
  /// The player's last known position, refreshed each render frame, for "move to player".
  bitematica::render::Vec3 mPlayerPosition{};

  ViewCamera mCamera;
  std::vector<bitematica::core::MaterialEntry> mMaterials;
  std::vector<bitematica::core::LiveMaterialRow> mLiveMaterials;
  std::vector<bitematica::render::ToolbarButton> mButtons;
  ControllerNavigator mNavigator;
  std::string mStatus;
  std::string mHint;
  bool mEnabled{false};

  // GLES side (render thread).
  bitematica::gles::SchematicRenderer mRenderer;
  std::vector<bitematica::gles::SectionBuffer> mSectionBuffers;

  // Async meshing.
  std::thread mMeshWorker;
  std::atomic<bool> mMeshWorkerStop{false};
  std::mutex mPendingMutex;
  PendingMesh mPending;
  std::atomic<bool> mMeshRequested{false};

  // World verification.
  bitematica::mod::GameWorldSource mWorldSource;
  bitematica::world::VerifyResult mVerification;
  std::atomic<bool> mVerifyRequested{false};
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
  installRenderCallback();
  mWorldSource.install();
  refreshStatusLine();
  submitHud();
  return true;
}

bool BiteMaticaMod::disable() {
  mEnabled = false;
  mMeshWorkerStop.store(true, std::memory_order_release);
  if (mMeshWorker.joinable()) mMeshWorker.join();
  pl::render::UnregisterRenderCallbacks(true);
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
      .description("Load, view, verify and export client-side schematics in world space.")
      .defaultEnabled(mConfig.enabled)
      .onToggle([this](std::string_view moduleId, bool enabled) {
        (void)moduleId;
        mConfig.enabled = enabled;
        if (!enabled) {
          const std::vector<pl::modmenu::DrawCommand> empty;
          pl::modmenu::submitDrawCommands(kModuleId, empty);
        } else {
          submitHud();
        }
        saveConfig();
      })
      .onConfigChanged([this](std::string_view, std::string_view key, std::string_view value) {
        const std::string k(key);
        const std::string v(value);
        if (k == "opacity") mConfig.opacity = std::atoi(v.c_str());
        else if (k == "renderMode")
          mConfig.renderMode = static_cast<bitematica::mod::RenderMode>(std::atoi(v.c_str()));
        else if (k == "layerMode")
          mConfig.layerMode = static_cast<bitematica::mod::LayerMode>(std::atoi(v.c_str()));
        else if (k == "showMaterialList") mConfig.showMaterialList = v == "true";
        else if (k == "ghostPlacement") mConfig.ghostPlacement = v == "true";
        else if (k == "verifyAgainstWorld") mConfig.verifyAgainstWorld = v == "true";
        mConfig.normalize();
        saveConfig();
        if (k == "layerMode") rebuildMeshAsync();
        submitHud();
      })
      .config("renderMode", "Render Mode", pl::modmenu::ConfigType::Radio,
              std::to_string(static_cast<int>(mConfig.renderMode)), "Overlay,Skeleton,Hidden")
      .config("opacity", "Opacity", pl::modmenu::ConfigType::SliderInt,
              std::to_string(mConfig.opacity), "10", "100")
      .config("layerMode", "Layers", pl::modmenu::ConfigType::Radio,
              std::to_string(static_cast<int>(mConfig.layerMode)), "All,Single,Range")
      .config("showMaterialList", "Material List", pl::modmenu::ConfigType::Toggle,
              mConfig.showMaterialList ? "true" : "false")
      .config("ghostPlacement", "Ghost Placement", pl::modmenu::ConfigType::Toggle,
              mConfig.ghostPlacement ? "true" : "false")
      .config("verifyAgainstWorld", "Verify World", pl::modmenu::ConfigType::Toggle,
              mConfig.verifyAgainstWorld ? "true" : "false");
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
  submitHud();
  return true;
}

bool BiteMaticaMod::handleKey(int keyCode, bool isDown) {
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
  submitHud();
  return true;
}

// --- render --------------------------------------------------------------------------

void BiteMaticaMod::installRenderCallback() {
  // Start the meshing worker once. It only ever builds meshes; upload and draw stay on the render
  // thread.
  if (!mMeshWorker.joinable()) {
    mMeshWorker = std::thread([this] {
      while (!mMeshWorkerStop.load(std::memory_order_acquire)) {
        if (!mMeshRequested.exchange(false, std::memory_order_acq_rel)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        }
        if (!mHasSchematic) continue;
        int layerMin = 0;
        int layerMax = 0;
        currentLayerRange(layerMin, layerMax);
        const auto built = bitematica::mesh::SectionMesher::build(
            mSchematic, mPlacement, layerMin, layerMax, mConfig.maxTriangles);
        {
          std::lock_guard<std::mutex> lock(mPendingMutex);
          mPending.sections = built.sections;
          mPending.emittedQuads = built.emittedQuads;
          mPending.truncated = built.truncated;
          mPending.ready = true;
        }
      }
    });
  }

  pl::render::RegisterRenderCallback([this](const pl::render::RenderContext &context) {
    if (!mEnabled || !mConfig.enabled) return;
    drawWorldSpace(context);
  });
}

void BiteMaticaMod::drawWorldSpace(const pl::render::RenderContext &context) {
  if (!mHasSchematic || mConfig.renderMode == bitematica::mod::RenderMode::Hidden) return;

  // Lazy GL init: the first render frame is the first time we are on the render thread with a
  // context, so this is where the program is compiled.
  if (!mRenderer.ready()) {
    if (!mRenderer.initialize()) return;
  }

  // Upload any mesh the worker finished.
  uploadPendingMesh();

  // Camera: prefer the game's real matrices; fall back to a first-person approximation.
  bitematica::render::Mat4 viewProjection;
  if (context.hasViewProjection) {
    for (int i = 0; i < 16; ++i) viewProjection.m[i] = context.viewProjection.m[i];
  } else {
    float aspect = 16.0f / 9.0f;
    if (context.viewportHeight > 0) {
      aspect = static_cast<float>(context.viewportWidth) / static_cast<float>(context.viewportHeight);
    }
    pl::render::Mat4 sdkProjection;
    if (!pl::render::ComputeViewProjection(70.0f, aspect, 0.05f, 1024.0f, sdkProjection)) {
      return; // no camera at all this frame: draw nothing rather than guess
    }
    for (int i = 0; i < 16; ++i) viewProjection.m[i] = sdkProjection.m[i];
  }

  // Track the player position for "move to player" and the distance cull.
  float position[3] = {0.0f, 0.0f, 0.0f};
  if (pl::render::ReadLocalPlayerPosition(position)) {
    mPlayerPosition = {position[0], position[1], position[2]};
  }

  const auto frustum = bitematica::render::Frustum::fromViewProjection(viewProjection);
  const bitematica::render::Vec3 origin{static_cast<float>(mPlacement.origin.x),
                                        static_cast<float>(mPlacement.origin.y + mPlacement.yOffset),
                                        static_cast<float>(mPlacement.origin.z)};
  const float yaw = static_cast<float>(static_cast<int>(mPlacement.rotation) * 90);

  const bitematica::gles::DrawMode mode =
      mConfig.verifyAgainstWorld
          ? bitematica::gles::DrawMode::Mismatch
          : (mConfig.renderMode == bitematica::mod::RenderMode::Skeleton
                 ? bitematica::gles::DrawMode::Outlines
                 : bitematica::gles::DrawMode::Translucent);

  mRenderer.draw(viewProjection, origin, yaw, mode,
                 static_cast<float>(mConfig.opacity) / 100.0f, mPlayerPosition,
                 static_cast<float>(mConfig.renderDistance), frustum, mSectionBuffers);
}

void BiteMaticaMod::uploadPendingMesh() {
  std::vector<bitematica::mesh::SectionMesh> sections;
  {
    std::lock_guard<std::mutex> lock(mPendingMutex);
    if (!mPending.ready) return;
    sections.swap(mPending.sections);
    mPending.ready = false;
  }

  // Release the old buffers and upload the new ones. This happens on the render thread because it
  // touches the GL context.
  for (auto &buffer : mSectionBuffers) mRenderer.releaseSection(buffer);
  mSectionBuffers.clear();
  mSectionBuffers.reserve(sections.size());
  for (const auto &section : sections) {
    bitematica::gles::SectionBuffer buffer;
    if (mRenderer.uploadSection(buffer, section)) {
      mSectionBuffers.push_back(buffer);
    }
  }
}

void BiteMaticaMod::rebuildMeshAsync() {
  mMeshRequested.store(true, std::memory_order_release);
}

// --- actions -------------------------------------------------------------------------

void BiteMaticaMod::currentLayerRange(int &outMin, int &outMax) const {
  mConfig.resolvedLayers(mSchematic.sizeY(), outMin, outMax);
}

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
      int mode = (static_cast<int>(mConfig.renderMode) + 1) % 3;
      mConfig.renderMode = static_cast<bitematica::mod::RenderMode>(mode);
      mConfig.normalize();
      saveConfig();
      refreshStatusLine();
      return true;
    }
    case Action::ToggleMaterials:
      mConfig.showMaterialList = !mConfig.showMaterialList;
      saveConfig();
      return true;
    case Action::RotateLeft:
      mPlacement.rotateCcw();
      rebuildMeshAsync();
      refreshStatusLine();
      return true;
    case Action::RotateRight:
    case Action::RotatePlacement:
      mPlacement.rotateCw();
      rebuildMeshAsync();
      refreshStatusLine();
      return true;
    case Action::ZoomIn:
      mConfig.renderDistance = std::min(mConfig.renderDistance + 32, 1024);
      saveConfig();
      refreshStatusLine();
      return true;
    case Action::ZoomOut:
      mConfig.renderDistance = std::max(mConfig.renderDistance - 32, 32);
      saveConfig();
      refreshStatusLine();
      return true;
    case Action::NudgeUp:
      mPlacement.yOffset += 1;
      refreshStatusLine();
      return true;
    case Action::NudgeDown:
      mPlacement.yOffset -= 1;
      refreshStatusLine();
      return true;
    case Action::ToggleGhost:
      mConfig.ghostPlacement = !mConfig.ghostPlacement;
      saveConfig();
      return true;
    case Action::ExportStructure:
      exportStructure();
      return true;
    case Action::ExportCsv:
      exportCsv();
      return true;
    case Action::Verify:
      mConfig.verifyAgainstWorld = !mConfig.verifyAgainstWorld;
      saveConfig();
      runVerification();
      return true;
    case Action::CycleLayers: {
      int mode = (static_cast<int>(mConfig.layerMode) + 1) % 3;
      mConfig.layerMode = static_cast<bitematica::mod::LayerMode>(mode);
      // A single layer defaults to the middle of the build so the change is visible.
      if (mConfig.layerMode == bitematica::mod::LayerMode::Single && mHasSchematic) {
        mConfig.layerSingle = mSchematic.sizeY() / 2;
      }
      mConfig.normalize();
      saveConfig();
      rebuildMeshAsync();
      refreshStatusLine();
      return true;
    }
    case Action::MoveToPlayer:
      moveToPlayer();
      rebuildMeshAsync();
      return true;
    case Action::CycleMirror:
      mPlacement.cycleMirror();
      rebuildMeshAsync();
      refreshStatusLine();
      return true;
    case Action::SaveLitematic:
      saveLitematic();
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
  if (mFiles.empty() || mFileIndex < 0 || mFileIndex >= static_cast<int>(mFiles.size())) {
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
  mLiveMaterials.clear();
  mVerification = bitematica::world::VerifyResult{};
  mConfig.lastLoadedFile = path;
  // Anchor the build at the player, so it appears where they are rather than at the origin.
  moveToPlayer();
  // Keep the legacy software view scale sane too (used for nothing now, but harmless).
  mCamera = ViewCamera{};
  saveConfig();
  rebuildMeshAsync();
  refreshStatusLine();
}

void BiteMaticaMod::moveToPlayer() {
  float position[3] = {0.0f, 0.0f, 0.0f};
  if (pl::render::ReadLocalPlayerPosition(position)) {
    mPlayerPosition = {position[0], position[1], position[2]};
  }
  // Place the schematic a few blocks in front of the player so it does not overlap them.
  mPlacement.origin = {static_cast<int>(mPlayerPosition.x) + 3,
                       static_cast<int>(mPlayerPosition.y),
                       static_cast<int>(mPlayerPosition.z)};
  mPlacement.yOffset = 0;
  setStatus("Placement moved to the player");
}

void BiteMaticaMod::runVerification() {
  if (!mHasSchematic) {
    setStatus("Nothing loaded to verify");
    return;
  }
  if (!mWorldSource.available()) {
    setStatus("Verification unavailable: " + mWorldSource.diagnostic());
    return;
  }
  int layerMin = 0;
  int layerMax = 0;
  currentLayerRange(layerMin, layerMax);
  mVerification = bitematica::world::Verifier::verify(mSchematic, mPlacement, mWorldSource,
                                                      layerMin, layerMax, mConfig.verifyExtras);
  mLiveMaterials = bitematica::core::LiveMaterials::build(mSchematic, mVerification);
  char buffer[160];
  std::snprintf(buffer, sizeof(buffer), "Verified: %lld correct, %lld missing, %lld wrong, %lld extra",
                mVerification.correct, mVerification.missing, mVerification.wrong,
                mVerification.extra);
  setStatus(buffer);
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
    // The player loads it with /structure load; that is the reliable cheat-route placement.
    setStatus("Exported. Run: /" + bitematica::world::CheatPlacement::loadCommandForPlacement(
                                    bitematica::world::CheatPlacement::structureNameFor(mSchematic),
                                    mPlacement));
  } else {
    setStatus("Export failed: " + error);
  }
}

void BiteMaticaMod::saveLitematic() {
  // Two behaviours, both useful: with a live world, capture the region the placement currently
  // covers straight out of the world (the selection tool); without one, re-save the loaded
  // schematic under its own name. Either way the result is a `.litematic` a Java player can open.
  const std::filesystem::path exports = mSelf->getDataDir() / "exports";
  std::error_code ec;
  std::filesystem::create_directories(exports, ec);

  std::string base = std::filesystem::path(mSchematic.name).filename().string();
  if (base.empty()) base = "bitematica_export";
  const std::filesystem::path outPath = exports / (base + ".litematic");

  std::string error;
  if (mHasSchematic && mWorldSource.available()) {
    // Capture the placement's world box into a fresh schematic, then write that.
    BlockPos min;
    BlockPos max;
    mPlacement.worldBounds(mSchematic, min, max);
    bitematica::world::Selection selection;
    selection.setCornerA(min);
    selection.setCornerB(max);
    Schematic captured;
    if (!selection.capture(mWorldSource, base, 2000000, captured, error)) {
      setStatus("Capture failed: " + error);
      return;
    }
    if (bitematica::format::LitematicWriter::write(captured, outPath.string(), base,
                                                   "BiteMatica", error)) {
      setStatus("Saved selection to " + outPath.string());
    } else {
      setStatus("Save failed: " + error);
    }
    return;
  }

  if (!mHasSchematic) {
    setStatus("Nothing loaded to save");
    return;
  }
  if (bitematica::format::LitematicWriter::write(mSchematic, outPath.string(), mSchematic.name,
                                                 "BiteMatica", error)) {
    setStatus("Saved to " + outPath.string());
  } else {
    setStatus("Save failed: " + error);
  }
}

void BiteMaticaMod::exportCsv() {
  if (!mHasSchematic) {
    setStatus("Nothing loaded to export");
    return;
  }
  const std::filesystem::path exports = mSelf->getDataDir() / "exports";
  std::error_code ec;
  std::filesystem::create_directories(exports, ec);
  const std::filesystem::path outPath = exports / "materials.csv";
  std::ofstream out(outPath, std::ios::trunc);
  if (!out) {
    setStatus("Could not write the CSV");
    return;
  }
  out << bitematica::core::LiveMaterials::toCsv(mLiveMaterials);
  setStatus("Materials written to " + outPath.string());
}

// --- HUD overlay ---------------------------------------------------------------------

void BiteMaticaMod::refreshStatusLine() {
  if (!mHasSchematic) {
    if (mStatus.empty()) setStatus("No schematic loaded");
    return;
  }
  const char *mode = mConfig.renderMode == bitematica::mod::RenderMode::Skeleton
                         ? "skeleton"
                         : (mConfig.renderMode == bitematica::mod::RenderMode::Hidden ? "hidden"
                                                                                     : "overlay");
  const char *layer = mConfig.layerMode == bitematica::mod::LayerMode::All
                          ? "all"
                          : (mConfig.layerMode == bitematica::mod::LayerMode::Single ? "layer"
                                                                                     : "range");
  char buffer[200];
  std::snprintf(buffer, sizeof(buffer), "%s  %dx%dx%d  %s  %s  rot %d  @%d,%d,%d",
                mSchematic.name.c_str(), mSchematic.sizeX(), mSchematic.sizeY(),
                mSchematic.sizeZ(), mode, layer, static_cast<int>(mPlacement.rotation) * 90,
                mPlacement.origin.x, mPlacement.origin.y, mPlacement.origin.z);
  setStatus(buffer);
}

void BiteMaticaMod::setStatus(const std::string &message) { mStatus = message; }

void BiteMaticaMod::submitHud() {
  mButtons = HudOverlay::layoutToolbar(mConfig);

  const pl::modmenu::HudSurfaceSize surface = pl::modmenu::getHudSurfaceSize();
  mCamera.centerX = surface.width > 0.0f ? surface.width * 0.5f : 640.0f;
  mCamera.centerY = surface.height > 0.0f ? surface.height * 0.52f : 360.0f;

  // The material panel shows the live list when a verification has run, otherwise the static one.
  std::vector<bitematica::core::MaterialEntry> displayMaterials = mMaterials;
  if (!mLiveMaterials.empty()) {
    displayMaterials.clear();
    for (const auto &row : mLiveMaterials) {
      displayMaterials.push_back({row.name, static_cast<uint64_t>(row.remaining())});
    }
  }

  const std::string infoLine = mHint.empty() ? mStatus : (mStatus + "   " + mHint);
  // The 2D overlay only draws the toolbar and material panel; the schematic itself is world-space
  // geometry now, so `hasSchematic` is false here to suppress the old software mesh.
  const std::vector<UiCommand> commands = HudOverlay::buildCommands(
      mSchematic, mConfig, mCamera, mButtons, displayMaterials, infoLine, false);

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
