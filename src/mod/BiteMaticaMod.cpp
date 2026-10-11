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
//
// Threading (Phase B13) — which thread owns which data:
//
//   * UI / input thread (touch, key, menu callbacks): owns `mConfig`, `mPlacement`, `mSelection`,
//     `mPlacements`, the menu state and the library. It never touches GL and never reads
//     `mSchematicPtr`'s contents except through `schematic()`. Changes publish a new
//     `RenderSnapshot` (under `mRenderMutex`) and a new `MeshRequest` (under `mRequestMutex`).
//   * Mesh worker thread: reads only the `MeshRequest` copy taken under `mRequestMutex`; it holds
//     the immutable `shared_ptr<const Schematic>` and never reads any live member. It writes the
//     finished meshes into `mPending` under `mPendingMutex`.
//   * Render thread (the SDK/EGL render callback): the only thread that makes GL calls. It reads
//     `RenderSnapshot` (under `mRenderMutex`), drains `mPending` (under `mPendingMutex`), uploads
//     buffers and draws. It writes `mPlayerPosition`, the frame counters and `mRenderError`.
//
// The rule that keeps this correct: the worker and the render thread never read `mSchematicPtr`'s
// pointee while it can change (it is only ever *replaced*, never mutated), and no GL call ever runs
// outside the render callback.

#include <pl/Input.hpp>
#include <pl/Mod.hpp>
#include <pl/ModMenu.hpp>
#include <pl/Render.hpp>

// Weak-links the render API so this same .AntEgg also loads on a preloader that predates it
// (GlowberryClient currently pins such a commit). Without this the mod's undefined pl::render::*
// symbols make dlopen fail and the launcher reports "missing symbol".
#include "mod/SdkCompat.hpp"

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
#include "gles/EglFrameSource.hpp"
#include "gles/SchematicRenderer.hpp"
#include "litematica/BlockTargeting.hpp"
#include "litematica/PasteJob.hpp"
#include "litematica/PlacementEdit.hpp"
#include "litematica/PlacementManager.hpp"
#include "litematica/PlacementStore.hpp"
#include "litematica/ToolMode.hpp"
#include "translate/BlockStateTransform.hpp"
#include "translate/BlockTranslation.hpp"
#include "mesh/SectionMesher.hpp"
#include "ui/MenuBuilder.hpp"
#include "ui/MenuModel.hpp"
#include "ui/MenuRenderer.hpp"
#include "mod/Config.hpp"
#include "mod/ControllerNavigator.hpp"
#include "mod/GameWorldSource.hpp"
#include "mod/JsonConfig.hpp"
#include "mod/LocalPlayerFeed.hpp"
#include "mod/SchematicLibrary.hpp"
#include "render/BlockColors.hpp"
#include "render/GameCamera.hpp"
#include "render/HudOverlay.hpp"
#include "render/MathTypes.hpp"
#include "render/PlacementCull.hpp"
#include "world/CheatPlacement.hpp"
#include "world/Placement.hpp"
#include "world/Selection.hpp"
#include "world/Verifier.hpp"

namespace {

using bitematica::core::Schematic;
using bitematica::core::BlockPos;
using bitematica::core::BlockState;
using bitematica::mod::Config;
using bitematica::mod::ControllerNavigator;
using bitematica::render::HudOverlay;
using bitematica::render::UiCommand;

constexpr const char *kModuleId = "bitematica.mod";
constexpr const char *kModuleName = "BiteMatica";

/// Draw-command colors the SDK expects are 0xAARRGGBB, matching `UiCommand`.
pl::modmenu::DrawCommandType toSdkKind(UiCommand::Kind kind) {
  switch (kind) {
    case UiCommand::Kind::Rect: return pl::modmenu::DrawCommandType::RectFilled;
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
  std::vector<bitematica::mesh::SectionMesher::SectionOutline> outlines;
  bool ready{false};
  long long emittedQuads{};
  bool truncated{};
};

/// An immutable snapshot of everything the mesher needs.
///
/// The worker thread must never read the mod's live state: a schematic load or a rotate happening
/// mid-mesh would otherwise race with it and crash. `rebuildMeshAsync` captures the placement and
/// the meshing-relevant config under a mutex.
///
/// The schematic is held by `shared_ptr<const Schematic>` rather than copied: a schematic can be
/// megabytes, and copying it on the calling thread every rebuild was needless work. Publishing an
/// immutable schematic once and sharing it keeps the snapshot race-free without the copy — the
/// pointed-to object is never mutated after publication.
struct MeshRequest {
  std::shared_ptr<const Schematic> schematic;
  bitematica::world::Placement placement;
  int layerMin{};
  int layerMax{};
  int maxTriangles{};
  bool outlines{};
  uint32_t outlineColor{};
  std::vector<uint8_t> statuses;
};

/// An immutable snapshot of the state the render callback reads.
///
/// The render callback runs on the render thread while touch/key/menu callbacks may mutate the
/// placement and config, so it reads this copy rather than the live members. Writers refresh it
/// under the mutex; the callback takes its own copy at the top of the frame.
struct RenderSnapshot {
  bool enabled{};
  bool hasSchematic{};
  int renderMode{};
  bool verifyAgainstWorld{};
  float opacity{1.0f};
  float renderDistance{256.0f};
  bool showOutlines{};
  float outlineWidth{1.5f};
  float polygonOffsetFactor{-1.0f};
  float polygonOffsetUnits{-1.0f};
  int mismatchMode{};
  bitematica::world::Placement placement;
};

/// All mutable mod state. Kept in one object so the callbacks can reach it without file-scope
/// globals scattered across the translation unit.
class BiteMaticaMod {
public:
  static BiteMaticaMod &instance() {
    static BiteMaticaMod mod;
    return mod;
  }

  BiteMaticaMod() = default;
  BiteMaticaMod(const BiteMaticaMod &) = delete;
  BiteMaticaMod &operator=(const BiteMaticaMod &) = delete;

  bool load();
  bool enable();
  bool disable();
  bool unload();

  /// Safety net: a joinable std::thread destroyed while joinable calls std::terminate, so the
  /// worker is stopped and joined if the object is ever destroyed without disable()/unload().
  ~BiteMaticaMod() {
    mMeshWorkerStop.store(true, std::memory_order_release);
    if (mMeshWorker.joinable()) mMeshWorker.join();
  }

private:
  void registerMenuModule();
  void installInputCallbacks();
  void installRenderCallback();
  void saveConfig();
  void scanLibrary();
  void loadCurrentSchematic();
  void refreshStatusLine();
  void submitHud();
  void refreshRenderSnapshot();
  void rebuildMeshAsync();
  void uploadPendingMesh();
  /// Frees every GL resource. Render thread only, with the context current.
  void releaseGlResources();
  void onRenderFrame();
  void drawWorldSpace(const bitematica::render::CameraFrame &camera);
  bool performAction(bitematica::render::ToolbarButton::Action action);
  bool handleTouch(float x, float y);
  bool handleKey(int keyCode, bool isDown);
  void exportStructure();
  void exportCsv();
  void saveLitematic();
  /// The running game version tag, best-effort. Empty when it cannot be read (the world-source gate
  /// then stays closed). Reads the version from the preloader's SDK when it exposes one.
  static std::string gameVersionTag();
  /// Loads/saves the placement list from/to `placements.json` (D1).
  void loadPlacements();
  void savePlacements();
  void applyTool();
  void runVerification();
  void moveToPlayer();
  void setStatus(const std::string &message);
  void currentLayerRange(int &outMin, int &outMax) const;

  ll::mod::NativeMod *mSelf{};
  Config mConfig;
  /// The loaded schematic, published immutably so the worker thread can share it without a copy.
  /// Empty (null) until a file loads. Never mutated in place: a load replaces the whole pointer.
  std::shared_ptr<const Schematic> mSchematicPtr;
  bool mHasSchematic{false};

  /// A reference to the loaded schematic, or a static empty one so callers need no null checks.
  const Schematic &schematic() const {
    static const Schematic kEmpty;
    return mSchematicPtr ? *mSchematicPtr : kEmpty;
  }
  std::vector<std::string> mFiles;
  int mFileIndex{0};

  /// Where the schematic sits in the world. Set to the player's position on load/move.
  bitematica::world::Placement mPlacement;
  /// The player's last known position, refreshed each render frame, for "move to player".
  bitematica::render::Vec3 mPlayerPosition{};

  std::vector<bitematica::core::MaterialEntry> mMaterials;
  std::vector<bitematica::core::LiveMaterialRow> mLiveMaterials;
  std::vector<bitematica::render::ToolbarButton> mButtons;
  ControllerNavigator mNavigator;
  std::string mStatus;
  std::string mHint;
  std::atomic<bool> mEnabled{false};
  /// Which seam delivers the per-frame render callback.
  enum class RenderSeam {
    None,       ///< no seam: nothing is drawn (the status line names the reason)
    Egl,        ///< `eglSwapBuffers`, version-independent (works on every 1.26 build)
    Preloader,  ///< the SDK render callback, only on a preloader that exports it
  };
  RenderSeam mRenderSeam{RenderSeam::None};

  // Diagnostics, so a failure is visible on screen instead of silent.
  std::atomic<unsigned long long> mFramesDrawn{0};
  std::atomic<unsigned long long> mFramesWithoutCamera{0};
  int mViewportWidth{0};
  int mViewportHeight{0};

  /// A one-time render failure reason (backend unusable, program would not link), set from the
  /// render thread and read by the status line.
  std::mutex mRenderErrorMutex;
  std::string mRenderError;
  std::atomic<bool> mRenderErrorSet{false};

  /// True when a per-frame seam was installed (EGL or preloader), so the schematic can be drawn as
  /// world-space GL geometry. False means nothing is drawn.
  bool worldSpaceActive() const { return mRenderSeam != RenderSeam::None; }

  // GLES side (render thread).
  bitematica::gles::SchematicRenderer mRenderer;
  std::vector<bitematica::gles::SectionBuffer> mSectionBuffers;
  std::vector<bitematica::gles::SectionBuffer> mOutlineBuffers;

  // Async meshing. The worker reads only a `MeshRequest` snapshot, never the live members.
  std::thread mMeshWorker;
  std::atomic<bool> mMeshWorkerStop{false};
  std::mutex mRequestMutex;
  MeshRequest mRequest;
  std::atomic<bool> mMeshRequested{false};
  std::mutex mPendingMutex;
  PendingMesh mPending;
  /// Set by disable()/unload(); the next render frame frees the GL resources with the context
  /// current, then unregisters the callbacks. Never touched off the render thread once set.
  std::atomic<bool> mGlShutdownRequested{false};

  // Render-thread snapshot, guarded by `mRenderMutex`.
  std::mutex mRenderMutex;
  RenderSnapshot mRenderSnapshot;

  // World verification.
  bitematica::mod::GameWorldSource mWorldSource;
  bitematica::world::VerifyResult mVerification;
  std::atomic<bool> mVerifyRequested{false};

  // Litematica toolset state.
  bitematica::litematica::ToolMode mToolMode{bitematica::litematica::ToolMode::AreaSelection};
  bitematica::litematica::PlacementManager mPlacements;
  bitematica::world::Selection mSelection;

  // Menu state. The pages are rebuilt whenever the underlying state changes, so the menu always
  // reflects the live config and placements.
  std::vector<bitematica::ui::MenuPage> mMenuPages;
  bool mMenuOpen{false};
  int mMenuTab{0};
  int mMenuScroll{0};
  /// Hitboxes from the last menu layout, matched against a tap.
  std::vector<bitematica::ui::MenuHit> mMenuHits;
  /// The panel bounds from the last layout, so a tap outside closes the menu.
  struct Panel {
    float x{};
    float y{};
    float w{};
    float h{};
  };
  Panel mMenuPanel{};
  float mMenuIconX{};
  float mMenuIconY{};
  float mMenuIconSize{52.0f};

  /// Retries the EGL hook install; called from enable and from input while the game is live.
  void retryRenderSeam();
  /// Rebuilds the menu pages from the live config and placement state.
  void rebuildMenuPages();
  /// Handles a tap while the menu is open. Returns true when the tap was consumed.
  bool handleMenuTouch(float x, float y);
  /// Applies a widget interaction (toggle, choice, slider, button).
  void activateWidget(bitematica::ui::Widget &widget);
  /// Writes a menu widget's value back into the config.
  void applyMenuConfig(const std::string &id, const std::string &value);
  /// Runs a menu button's action.
  void applyMenuAction(const std::string &id);
};

// --- lifecycle ----------------------------------------------------------------------

bool BiteMaticaMod::load() {
  mSelf = ll::mod::NativeMod::current();
  if (mSelf == nullptr) return false;
  auto &log = mSelf->getLogger();

  std::error_code ec;
  std::filesystem::create_directories(mSelf->getConfigDir(), ec);
  std::filesystem::create_directories(mSelf->getDataDir() / "exports", ec);

  // Load the block-colour table from the package (`data/block_colors.json`), overlaying the embedded
  // default. A missing file is not an error: the default table is always usable. The count is logged
  // so a package that forgot the file is visible.
  {
    const std::filesystem::path colorsPath = mSelf->getModDir() / "data" / "block_colors.json";
    const int merged = bitematica::render::BlockColors::loadFile(colorsPath.string());
    log.info("BiteMatica: block colour table: {} entries merged from {}", merged,
             merged > 0 ? colorsPath.string() : "(embedded default)");
  }
  {
    // The Java->Bedrock translation table (G2), merged over the built-in renames.
    const std::filesystem::path translationPath =
        mSelf->getModDir() / "data" / "block_translation.json";
    const int merged = bitematica::translate::BlockTranslation::loadFile(translationPath.string());
    log.info("BiteMatica: block translation table: {} renames merged", merged);
  }

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
  loadPlacements();
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
  mEnabled.store(true, std::memory_order_release);
  registerMenuModule();
  installInputCallbacks();
  // Hook the local player first: it is the camera source, and it is the mechanism proven across
  // 1.26.33 / 1.26.50 / 1.26.51, so it is what makes world-space rendering work on those builds.
  bitematica::mod::LocalPlayerFeed::install();

  // Install the per-frame render seam. The SDK render callback is the primary path; the EGL hook is
  // the fallback. When neither is available the mod draws no schematic and says so on screen — it
  // never falls back to a screen-space overlay, which is not world-space rendering.
  installRenderCallback();
  if (mSelf != nullptr) {
    const char *seam = mRenderSeam == RenderSeam::Preloader
                           ? "preloader render callback"
                           : (mRenderSeam == RenderSeam::Egl ? "eglSwapBuffers (version-independent)"
                                                             : "none (no world-space seam)");
    mSelf->getLogger().info("BiteMatica: render seam = {}", seam);
  }
  // Configure the world source. It stays available()==false unless a slot is set, a confirmed
  // reader is installed, the configured version tag matches the running game version, and the
  // sanity probe passes — so verification reports "world not readable" rather than guessing. The
  // version string is best-effort; when it cannot be read the gate stays closed (fail-closed).
  mWorldSource.configure(mConfig.verifyBlockSourceSlot, mConfig.verifyGameVersionTag,
                         gameVersionTag());
  refreshStatusLine();
  submitHud();
  return true;
}

bool BiteMaticaMod::disable() {
  mEnabled.store(false, std::memory_order_release);
  mMeshWorkerStop.store(true, std::memory_order_release);
  if (mMeshWorker.joinable()) mMeshWorker.join();
  pl::modmenu::unregisterModule(kModuleId);

  // GL resources must be freed with the context current, which only the render thread has. If a
  // seam is live, leave it registered and let the next frame free the buffers, then unregister the
  // callbacks itself; if no seam is live there is nothing to free, so unregister now.
  if (worldSpaceActive()) {
    mGlShutdownRequested.store(true, std::memory_order_release);
  } else {
    bitematica::gles::EglFrameSource::setCallback(nullptr);
    bitematica::mod::SdkRender::unregisterCallbacks(true);
  }
  if (mSelf != nullptr) mSelf->getLogger().info("BiteMatica disabled");
  return true;
}

bool BiteMaticaMod::unload() {
  mEnabled.store(false, std::memory_order_release);
  // A joinable std::thread destroyed while still joinable calls std::terminate, so the worker must
  // be stopped and joined here too, not only in disable().
  mMeshWorkerStop.store(true, std::memory_order_release);
  if (mMeshWorker.joinable()) mMeshWorker.join();
  // Ask for a GL teardown on the next render frame (the only place the context is current). If no
  // seam is live, the resources were never created, so there is nothing to free.
  if (worldSpaceActive()) {
    mGlShutdownRequested.store(true, std::memory_order_release);
  } else {
    bitematica::gles::EglFrameSource::setCallback(nullptr);
    bitematica::mod::SdkRender::unregisterCallbacks(true);
  }
  return true;
}

void BiteMaticaMod::releaseGlResources() {
  // Render thread only, with the GL context current.
  for (auto &buffer : mSectionBuffers) mRenderer.releaseSection(buffer);
  mSectionBuffers.clear();
  for (auto &buffer : mOutlineBuffers) mRenderer.releaseSection(buffer);
  mOutlineBuffers.clear();
  mRenderer.shutdown();
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
    if (!mEnabled.load(std::memory_order_acquire)) return false;
    if (event.action != 0 /* ACTION_DOWN */) return false;
    return handleTouch(event.x, event.y);
  });
  pl::input::registerKeyCallback([this](const pl::input::KeyEvent &event) {
    if (!mEnabled.load(std::memory_order_acquire)) return false;
    return handleKey(event.keyCode, event.isKeyDown);
  });
}

bool BiteMaticaMod::handleTouch(float x, float y) {
  // Input only arrives while the game is live, so it is the reliable moment to finish installing the
  // EGL hook if the game had not mapped a GLES library when the mod enabled.
  retryRenderSeam();

  // The menu (and its floating icon) is checked first, so a tap on the icon or a menu row is never
  // swallowed by the toolbar underneath.
  if (handleMenuTouch(x, y)) return true;

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
  // Start the meshing worker if it is not already running. `disable()` joins it, so a re-enable
  // must clear the stop flag before starting a new one — otherwise the new thread sees the stale
  // `true` and exits immediately, and nothing ever meshes again.
  if (!mMeshWorker.joinable()) {
    mMeshWorkerStop.store(false, std::memory_order_release);
    mMeshWorker = std::thread([this] {
      while (!mMeshWorkerStop.load(std::memory_order_acquire)) {
        if (!mMeshRequested.exchange(false, std::memory_order_acq_rel)) {
          std::this_thread::sleep_for(std::chrono::milliseconds(50));
          continue;
        }
        // Take a private copy of everything needed. The live members can be mutated by the touch
        // and menu callbacks at any moment, so the worker must never touch them directly.
        MeshRequest request;
        {
          std::lock_guard<std::mutex> lock(mRequestMutex);
          request = mRequest;
        }
        if (!request.schematic || request.schematic->empty()) continue;

        const auto built = bitematica::mesh::SectionMesher::build(
            *request.schematic, request.placement, request.layerMin, request.layerMax,
            request.maxTriangles, &request.statuses);
        // The outline (block-edge) geometry is built here too, on the worker, so the render thread
        // only ever uploads buffers and draws.
        std::vector<bitematica::mesh::SectionMesher::SectionOutline> outlines;
        if (request.outlines) {
          auto builtOutlines = bitematica::mesh::SectionMesher::buildOutlines(
              *request.schematic, request.placement, request.layerMin, request.layerMax,
              request.outlineColor);
          outlines = std::move(builtOutlines.sections);
        }
        {
          std::lock_guard<std::mutex> lock(mPendingMutex);
          mPending.sections = std::move(built.sections);
          mPending.outlines = std::move(outlines);
          mPending.emittedQuads = built.emittedQuads;
          mPending.truncated = built.truncated;
          mPending.ready = true;
        }
      }
    });
  }

  // The frame source is chosen at runtime, and both paths end in the same `drawWorldSpace`:
  //
  //  * The **preloader** path uses the SDK's render callback (`pl/Render.hpp`). It runs on the render
  //    thread with the GL context current and hands the mod the game's real camera matrices, so it
  //    is the primary seam whenever the running preloader exports it.
  //  * The **EGL** path hooks `eglSwapBuffers`, which every GLES build calls once per frame. It is
  //    version-independent and is the fallback for a preloader that predates the render API; the
  //    camera then comes from the local player feed.
  //
  // Neither path is a screen-space overlay: if neither is available, nothing is drawn.
  bitematica::gles::EglFrameSource::setCallback([this] { onRenderFrame(); });
  if (bitematica::mod::SdkRender::available()) {
    bitematica::mod::SdkRender::registerCallback([this](const pl::render::RenderContext &) {
      onRenderFrame();
    });
    mRenderSeam = RenderSeam::Preloader;
  }
  retryRenderSeam();
}

void BiteMaticaMod::retryRenderSeam() {
  // Called from `enable` and again from every touch/key event, so the EGL hook installs as soon as
  // the game has loaded its GLES library. Idempotent and cheap once it succeeds.
  //
  // The SDK render callback is preferred when present (it carries the game's real camera matrices);
  // EGL is the fallback seam. A seam already found is never downgraded.
  if (mRenderSeam != RenderSeam::None) return;
  if (bitematica::mod::SdkRender::available()) {
    mRenderSeam = RenderSeam::Preloader;
  } else if (bitematica::gles::EglFrameSource::install()) {
    mRenderSeam = RenderSeam::Egl;
  }
}

void BiteMaticaMod::onRenderFrame() {
  // Called on the render thread with the GL context current, from whichever seam was installed.

  // A pending teardown is serviced first: this is the only place the context is current, so it is
  // where the buffers and program must be freed. After freeing, the callbacks are unregistered so
  // no further frame reaches this function.
  if (mGlShutdownRequested.exchange(false, std::memory_order_acq_rel)) {
    releaseGlResources();
    bitematica::gles::EglFrameSource::setCallback(nullptr);
    bitematica::mod::SdkRender::unregisterCallbacks(true);
    mRenderSeam = RenderSeam::None;
    return;
  }

  // Cheap early-out on the snapshot's enabled flag, then the real draw.
  {
    std::lock_guard<std::mutex> lock(mRenderMutex);
    if (!mRenderSnapshot.enabled) return;
  }

  // The camera is built from the local player feed, which is hooked on the accessor the game itself
  // calls every frame. This is the same mechanism the launcher's own in-world features use, and it
  // is proven across 1.26.33 / 1.26.50 / 1.26.51 — so it needs neither the SDK render callback nor a
  // speculative matrix scan.
  bitematica::render::CameraFrame camera;
  camera.viewportWidth = mViewportWidth;
  camera.viewportHeight = mViewportHeight;

  // The real game matrices are preferred when they are available (a newer launcher), but the
  // player-derived camera is always there as the reliable path.
  if (mRenderSeam == RenderSeam::Egl) {
    int width = 0;
    int height = 0;
    bitematica::gles::EglFrameSource::readViewport(width, height);
    if (width > 0 && height > 0) {
      mViewportWidth = width;
      mViewportHeight = height;
      camera.viewportWidth = width;
      camera.viewportHeight = height;
    }
  }

  float aspect = 16.0f / 9.0f;
  if (camera.viewportHeight > 0) {
    aspect = static_cast<float>(camera.viewportWidth) / static_cast<float>(camera.viewportHeight);
  }
  // The local-player camera is the reliable path (it works on every build the feed works on) and it
  // gives `view` and `projection` separately, which the camera-relative renderer needs.
  bitematica::render::Mat4 viewProjection;
  bitematica::render::Mat4 view;
  bitematica::render::Mat4 projection;
  bitematica::render::Vec3 eye;
  bitematica::render::Vec3 forward;
  if (bitematica::mod::LocalPlayerFeed::buildLocalCamera(70.0f, aspect, 0.05f, 1024.0f,
                                                         viewProjection, eye, forward, view,
                                                         projection)) {
    camera.viewProjection = viewProjection;
    camera.view = view;
    camera.projection = projection;
    camera.position = eye;
    camera.forward = forward;
    camera.valid = true;
  }
  drawWorldSpace(camera);
}

void BiteMaticaMod::drawWorldSpace(const bitematica::render::CameraFrame &camera) {
  // Read a private copy: the live members can be mutated by input on another thread mid-frame.
  RenderSnapshot snapshot;
  {
    std::lock_guard<std::mutex> lock(mRenderMutex);
    snapshot = mRenderSnapshot;
  }
  if (!snapshot.hasSchematic ||
      snapshot.renderMode == static_cast<int>(bitematica::mod::RenderMode::Hidden)) {
    return;
  }

  // Lazy GL init: the first render frame is the first time we are on the render thread with a
  // context, so this is where the program is compiled. A failure (non-GLES-3 backend, or a program
  // that will not link) is recorded once with its reason, so the status line can name it; nothing is
  // drawn.
  if (!mRenderer.ready()) {
    if (!mRenderer.initialize()) {
      if (!mRenderErrorSet.exchange(true, std::memory_order_acq_rel)) {
        std::lock_guard<std::mutex> lock(mRenderErrorMutex);
        mRenderError = std::string("world-space renderer unavailable: ") +
                       mRenderer.backendDescription();
      }
      return;
    }
  }

  // Upload any mesh the worker finished.
  uploadPendingMesh();

  // The camera is already a validated world->clip matrix (from the player feed or the game's own
  // matrices). If it is not valid there is no live player, so there is nothing to draw against.
  if (!camera.valid) {
    mFramesWithoutCamera.fetch_add(1, std::memory_order_relaxed);
    return;
  }
  const bitematica::render::Mat4 &viewProjection = camera.viewProjection;

  // Track the camera position for "move to player" and the distance cull.
  mPlayerPosition = camera.position;
  mFramesDrawn.fetch_add(1, std::memory_order_relaxed);

  const bitematica::world::Placement &placement = snapshot.placement;
  const bitematica::render::Vec3 origin{static_cast<float>(placement.origin.x),
                                        static_cast<float>(placement.origin.y + placement.yOffset),
                                        static_cast<float>(placement.origin.z)};
  const float yaw = static_cast<float>(static_cast<int>(placement.rotation) * 90);

  // Build the camera-relative mvp and the placement-local cull from the same inputs. The transform
  // chain is applied exactly once: the CPU subtracts the camera position in double precision (inside
  // `computePlacementCull`) and the shader applies the placement's yaw and relative origin. The mvp
  // never contains a world translation, so it stays exact at world coordinate ±3e7.
  const bitematica::render::PlacementCull cull = bitematica::render::computePlacementCull(
      camera.projection, camera.view, origin, yaw, mPlayerPosition);

  bitematica::gles::DrawOptions options;
  options.mode = snapshot.verifyAgainstWorld
                     ? bitematica::gles::DrawMode::Mismatch
                     : (snapshot.renderMode == static_cast<int>(bitematica::mod::RenderMode::Skeleton)
                            ? bitematica::gles::DrawMode::Outlines
                            : bitematica::gles::DrawMode::Translucent);
  options.alpha = snapshot.opacity;
  options.maxDistance = snapshot.renderDistance;
  options.polygonOffsetFactor = snapshot.polygonOffsetFactor;
  options.polygonOffsetUnits = snapshot.polygonOffsetUnits;
  options.outlines = snapshot.showOutlines;
  options.outlineWidth = snapshot.outlineWidth;
  options.mismatchMode = snapshot.mismatchMode;
  // The palette preset (C6/J9) selects the mismatch colours; the shader receives them as a uniform.
  options.palette = bitematica::world::MismatchPalette::preset(mConfig.palettePreset);

  mRenderer.draw(cull.mvp, cull.cameraRelativeOrigin, yaw, options, cull.localCamera, cull.frustum,
                 mSectionBuffers, mOutlineBuffers);
}

void BiteMaticaMod::uploadPendingMesh() {
  std::vector<bitematica::mesh::SectionMesh> sections;
  std::vector<bitematica::mesh::SectionMesher::SectionOutline> outlines;
  {
    std::lock_guard<std::mutex> lock(mPendingMutex);
    if (!mPending.ready) return;
    sections.swap(mPending.sections);
    outlines.swap(mPending.outlines);
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

  // Upload the outline (block-edge) buffers for this same mesh generation.
  for (auto &buffer : mOutlineBuffers) mRenderer.releaseSection(buffer);
  mOutlineBuffers.clear();
  mOutlineBuffers.reserve(outlines.size());
  for (const auto &outline : outlines) {
    bitematica::gles::SectionBuffer buffer;
    const bitematica::render::Vec3 min{outline.originX, outline.originY, outline.originZ};
    const bitematica::render::Vec3 max{outline.originX + bitematica::mesh::SectionMesher::kSectionSize,
                                       outline.originY + bitematica::mesh::SectionMesher::kSectionSize,
                                       outline.originZ + bitematica::mesh::SectionMesher::kSectionSize};
    if (mRenderer.uploadOutlines(buffer, outline.vertices, outline.segments, min, max)) {
      mOutlineBuffers.push_back(buffer);
    }
  }
}

void BiteMaticaMod::refreshRenderSnapshot() {
  // Called after any change the render callback depends on. The callback reads only this copy, so
  // the placement and config can be mutated by input on another thread without racing the render.
  std::lock_guard<std::mutex> lock(mRenderMutex);
  // The panic toggle (J6) disables drawing instantly without unloading: it gates the same flag the
  // render callback checks first, so nothing is drawn and no GL work happens until it is turned off.
  mRenderSnapshot.enabled = mConfig.enabled && !mConfig.panicHidden;
  mRenderSnapshot.hasSchematic = mHasSchematic;
  mRenderSnapshot.renderMode = static_cast<int>(mConfig.renderMode);
  mRenderSnapshot.verifyAgainstWorld = mConfig.verifyAgainstWorld;
  mRenderSnapshot.opacity = static_cast<float>(mConfig.opacity) / 100.0f;
  mRenderSnapshot.renderDistance = static_cast<float>(mConfig.renderDistance);
  mRenderSnapshot.showOutlines = mConfig.showOutlines;
  mRenderSnapshot.outlineWidth = mConfig.outlineWidth;
  mRenderSnapshot.polygonOffsetFactor = mConfig.polygonOffsetFactor;
  mRenderSnapshot.polygonOffsetUnits = mConfig.polygonOffsetUnits;
  mRenderSnapshot.mismatchMode = mConfig.verifyMismatchMode;
  mRenderSnapshot.placement = mPlacement;
}

void BiteMaticaMod::rebuildMeshAsync() {
  // When no world-space seam is available nothing is drawn, so meshing would be pointless work.
  if (!worldSpaceActive()) return;
  // Snapshot the meshing inputs under the lock, then flag the worker. The worker reads only the
  // snapshot, so a load/rotate/edit on another thread cannot race it.
  int layerMin = 0;
  int layerMax = 0;
  currentLayerRange(layerMin, layerMax);
  {
    std::lock_guard<std::mutex> lock(mRequestMutex);
    // Share the immutable schematic rather than copying it; only the small placement/config fields
    // are copied.
    mRequest.schematic = mSchematicPtr;
    mRequest.placement = mPlacement;
    mRequest.layerMin = layerMin;
    mRequest.layerMax = layerMax;
    mRequest.maxTriangles = mConfig.maxTriangles;
    mRequest.outlines = mConfig.showOutlines;
    // Outline colour: a light accent that reads against terrain. Verification recolours it later.
    mRequest.outlineColor = 0xE64AE0A0u;
    // Per-block verdicts (F6) so the mesh carries the mismatch state; the shader colours and filters
    // by them, so changing the display mode is a uniform change, not a remesh. Copied under the
    // lock because the verifier writes mVerification on the UI thread.
    mRequest.statuses.clear();
    mRequest.statuses.reserve(mVerification.statuses.size());
    for (bitematica::world::BlockStatus status : mVerification.statuses) {
      mRequest.statuses.push_back(static_cast<uint8_t>(status));
    }
  }
  mMeshRequested.store(true, std::memory_order_release);
}

// --- actions -------------------------------------------------------------------------

void BiteMaticaMod::currentLayerRange(int &outMin, int &outMax) const {
  mConfig.resolvedLayers(schematic().sizeY(), outMin, outMax);
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
      mPlacements.pushUndo();
      mPlacement.rotateCcw();
      rebuildMeshAsync();
      savePlacements();
      refreshStatusLine();
      return true;
    case Action::RotateRight:
    case Action::RotatePlacement:
      mPlacements.pushUndo();
      mPlacement.rotateCw();
      rebuildMeshAsync();
      savePlacements();
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
      mPlacements.pushUndo();
      mPlacement.yOffset += 1;
      savePlacements();
      refreshStatusLine();
      return true;
    case Action::NudgeDown:
      mPlacements.pushUndo();
      mPlacement.yOffset -= 1;
      savePlacements();
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
        mConfig.layerSingle = schematic().sizeY() / 2;
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
      mPlacements.pushUndo();
      mPlacement.cycleMirror();
      rebuildMeshAsync();
      savePlacements();
      refreshStatusLine();
      return true;
    case Action::SaveLitematic:
      saveLitematic();
      return true;
    case Action::CycleToolMode:
      mToolMode = bitematica::litematica::nextToolMode(mToolMode);
      refreshStatusLine();
      submitHud();
      return true;
    case Action::ApplyTool:
      applyTool();
      return true;
    case Action::OpenMenu:
      mMenuOpen = !mMenuOpen;
      refreshStatusLine();
      submitHud();
      return true;
    case Action::CycleMenuTab:
      mMenuTab = (mMenuTab + 1) % bitematica::ui::kMenuTabCount;
      submitHud();
      return true;
    case Action::PanicToggle:
      // J6: instant hide/show without unloading. The snapshot's enabled flag gates the render
      // callback, so nothing is drawn (and no GL work happens) while it is on.
      mConfig.panicHidden = !mConfig.panicHidden;
      saveConfig();
      refreshRenderSnapshot();
      refreshStatusLine();
      submitHud();
      return true;
    case Action::Reload:
      scanLibrary();
      if (!mFiles.empty()) {
        if (mFileIndex >= static_cast<int>(mFiles.size())) mFileIndex = 0;
        loadCurrentSchematic();
      } else {
        mHasSchematic = false;
        mSchematicPtr.reset();
        setStatus("No schematic found");
      }
      return true;
  }
  return false;
}

// --- loading -------------------------------------------------------------------------

std::string BiteMaticaMod::gameVersionTag() {
  // The preloader SDK exposes the *mod's* version (`NativeMod::getVersion`), not the game's, and the
  // game does not export a version symbol. So the running game version cannot be read reliably here,
  // and this returns empty. The world-source gate treats an empty running tag as "not auto-verified"
  // and relies on the configured tag plus the sanity probe. When the SDK gains a game-version
  // accessor, return it here and the gate becomes a strict match.
  return {};
}

void BiteMaticaMod::loadPlacements() {
  if (mSelf == nullptr) return;
  const std::filesystem::path path = mSelf->getDataDir() / "placements.json";
  std::vector<bitematica::litematica::PlacedSchematic> loaded;
  std::string error;
  if (!bitematica::litematica::PlacementStore::readFile(path.string(), loaded, error)) {
    // No file yet is normal on a first run; a corrupt file is worth a log line.
    if (std::filesystem::exists(path)) {
      mSelf->getLogger().warn("BiteMatica: placements not loaded: {}", error);
    }
    return;
  }
  // Resolve each placement's schematic from its id. A placement whose file is gone is kept but left
  // with a null schematic, so the placement list shows it as "(missing)" rather than dropping it.
  for (auto &placement : loaded) {
    if (placement.schematicId.empty()) continue;
    const bitematica::format::LoadResult result =
        bitematica::format::SchematicFormat::readPath(placement.schematicId);
    if (result.success) {
      placement.schematic = std::make_shared<const Schematic>(std::move(result.schematic));
    } else if (mSelf != nullptr) {
      mSelf->getLogger().warn("BiteMatica: placement schematic {} could not be re-read: {}",
                             placement.schematicId, result.error);
    }
  }
  for (auto &placement : loaded) mPlacements.add(placement.schematic, placement.placement);
  // Restore the per-placement settings the `add` path does not carry.
  for (size_t i = 0; i < loaded.size(); ++i) {
    if (auto *entry = mPlacements.at(static_cast<int>(i))) {
      entry->schematicId = loaded[i].schematicId;
      entry->displayName = loaded[i].displayName;
      entry->visible = loaded[i].visible;
      entry->locked = loaded[i].locked;
      entry->layerMode = loaded[i].layerMode;
      entry->layerAxis = loaded[i].layerAxis;
      entry->layerMin = loaded[i].layerMin;
      entry->layerMax = loaded[i].layerMax;
      entry->opacity = loaded[i].opacity;
      entry->showOutlines = loaded[i].showOutlines;
    }
  }
  if (!loaded.empty() && mSelf != nullptr) {
    mSelf->getLogger().info("BiteMatica: restored {} placement(s)", loaded.size());
  }
}

void BiteMaticaMod::savePlacements() {
  if (mSelf == nullptr) return;
  const std::filesystem::path path = mSelf->getDataDir() / "placements.json";
  std::string error;
  if (!bitematica::litematica::PlacementStore::writeFile(path.string(), mPlacements.all(), error)) {
    mSelf->getLogger().warn("BiteMatica: placements not saved: {}", error);
  }
}

void BiteMaticaMod::scanLibrary() {
  const std::filesystem::path dir = mSelf->getModDir() / "schematics";
  mFiles = bitematica::mod::SchematicLibrary::scan(dir.string());
  if (mFileIndex >= static_cast<int>(mFiles.size())) mFileIndex = 0;
}

void BiteMaticaMod::loadCurrentSchematic() {
  if (mFiles.empty() || mFileIndex < 0 || mFileIndex >= static_cast<int>(mFiles.size())) {
    mHasSchematic = false;
    mSchematicPtr.reset();
    return;
  }
  const std::string path = mFiles[static_cast<size_t>(mFileIndex)];
  const bitematica::format::LoadResult result =
      bitematica::format::SchematicFormat::readPath(path);
  if (!result.success) {
    mHasSchematic = false;
    mSchematicPtr.reset();
    setStatus("Load failed: " + result.error);
    if (mSelf != nullptr) mSelf->getLogger().warn("Failed to load {}: {}", path, result.error);
    return;
  }
  // Publish the parsed schematic immutably. The worker and the render callback read this pointer;
  // nothing mutates the pointed-to object afterward.
  mSchematicPtr = std::make_shared<const Schematic>(std::move(result.schematic));
  mHasSchematic = true;
  mMaterials = bitematica::core::MaterialList::build(schematic());
  mLiveMaterials.clear();
  mVerification = bitematica::world::VerifyResult{};
  mConfig.lastLoadedFile = path;
  refreshRenderSnapshot();
  // Anchor the build at the player, so it appears where they are rather than at the origin.
  moveToPlayer();
  saveConfig();
  rebuildMeshAsync();
  refreshStatusLine();
}

void BiteMaticaMod::moveToPlayer() {
  mPlacements.pushUndo();
  float position[3] = {0.0f, 0.0f, 0.0f};
  if (bitematica::mod::SdkRender::readLocalPlayerPosition(position)) {
    mPlayerPosition = {position[0], position[1], position[2]};
  }
  // Place the schematic a few blocks in front of the player so it does not overlap them.
  mPlacement.origin = {static_cast<int>(mPlayerPosition.x) + 3,
                       static_cast<int>(mPlayerPosition.y),
                       static_cast<int>(mPlayerPosition.z)};
  mPlacement.yOffset = 0;
  refreshRenderSnapshot();
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
  // The sanity probe (F1) runs once, before the first real read, at positions whose values are
  // known: the block under the player's feet must not be air, and 400 blocks above must be. A reader
  // that fails the probe is disabled for the session, so a mis-set slot cannot colour the schematic
  // wrong. The probe needs a live player position.
  {
    float feet[3] = {0.0f, 0.0f, 0.0f};
    if (bitematica::mod::LocalPlayerFeed::readPosition(feet)) {
      const BlockPos feetBlock{static_cast<int>(std::floor(feet[0])),
                               static_cast<int>(std::floor(feet[1])),
                               static_cast<int>(std::floor(feet[2]))};
      const BlockPos skyBlock{feetBlock.x, feetBlock.y + 400, feetBlock.z};
      if (!mWorldSource.runProbe(feetBlock, skyBlock)) {
        setStatus("Verification unavailable: " + mWorldSource.diagnostic());
        return;
      }
    }
    // No live player yet: the probe cannot run, so the first read stays off (readBlock returns false
    // until the probe has passed) and the verifier reports Unknown rather than Missing.
  }
  int layerMin = 0;
  int layerMax = 0;
  currentLayerRange(layerMin, layerMax);
  mVerification = bitematica::world::Verifier::verify(schematic(), mPlacement, mWorldSource,
                                                      layerMin, layerMax, mConfig.verifyExtras);
  mLiveMaterials = bitematica::core::LiveMaterials::build(schematic(), mVerification);
  // Rebuild the mesh so the per-vertex verdicts (F6) match this verification. The mismatch display
  // mode itself is a uniform, so only a new verification (not a mode change) remeshes.
  rebuildMeshAsync();
  char buffer[200];
  std::snprintf(buffer, sizeof(buffer),
                "Verified: %lld correct, %lld missing, %lld wrong block, %lld wrong state, "
                "%lld extra, %lld unknown (%d%%)",
                mVerification.correct, mVerification.missing, mVerification.wrongBlock,
                mVerification.wrongState, mVerification.extra, mVerification.unknown,
                mVerification.percentComplete());
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
  std::string base = std::filesystem::path(schematic().name).filename().string();
  if (base.empty()) base = "bitematica_export";
  const std::filesystem::path outPath = exports / (base + ".mcstructure");
  std::string error;
  if (bitematica::format::StructureWriter::write(schematic(), outPath.string(), error)) {
    // The player loads it with /structure load; that is the reliable cheat-route placement.
    setStatus("Exported. Run: /" + bitematica::world::CheatPlacement::loadCommandForPlacement(
                                    bitematica::world::CheatPlacement::structureNameFor(schematic()),
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

  std::string base = std::filesystem::path(schematic().name).filename().string();
  if (base.empty()) base = "bitematica_export";
  const std::filesystem::path outPath = exports / (base + ".litematic");

  std::string error;
  if (mHasSchematic && mWorldSource.available()) {
    // Capture the placement's world box into a fresh schematic, then write that.
    BlockPos min;
    BlockPos max;
    mPlacement.worldBounds(schematic(), min, max);
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
  if (bitematica::format::LitematicWriter::write(schematic(), outPath.string(), schematic().name,
                                                 "BiteMatica", error)) {
    setStatus("Saved to " + outPath.string());
  } else {
    setStatus("Save failed: " + error);
  }
}

void BiteMaticaMod::applyTool() {
  // The nine Litematica tool modes, acting on the block under the crosshair. The world is only
  // readable when a block source is configured; a mode that needs the world says so rather than
  // acting on a guess.
  using bitematica::litematica::ToolMode;

  // Where is the player looking? The camera forward is the view direction; the eye is the origin.
  bitematica::render::CameraFrame camera = bitematica::render::GameCamera::read();
  bitematica::render::Vec3 origin = camera.valid ? camera.position : mPlayerPosition;
  bitematica::render::Vec3 direction = camera.valid ? camera.forward
                                                    : bitematica::render::Vec3{0.0f, 0.0f, -1.0f};

  const auto hit = bitematica::litematica::BlockTargeting::cast(
      origin, direction, 64.0f, [this](int x, int y, int z) {
        BlockState state;
        if (!mWorldSource.available()) return false;
        return mWorldSource.readBlock(BlockPos{x, y, z}, state) && !state.isAir();
      });

  switch (mToolMode) {
    case ToolMode::AreaSelection: {
      if (!hit.hit) {
        setStatus("Area Selection: look at a block to set a corner");
        return;
      }
      // Alternate corners: the first tap sets corner 1, the next corner 2.
      if (!mSelection.complete()) {
        if (mSelection.cornerA() == hit.block && mSelection.complete()) {
          mSelection.setCornerB(hit.block);
        } else if (mSelection.cornerA() == BlockPos{} && !mSelection.complete()) {
          mSelection.setCornerA(hit.block);
          mSelection.setCornerB(hit.block);
        } else {
          mSelection.setCornerB(hit.block);
        }
      } else {
        mSelection.setCornerA(hit.block);
        mSelection.setCornerB(hit.block);
      }
      char buffer[128];
      std::snprintf(buffer, sizeof(buffer), "Selection %lldx%lldx%lld (%lld blocks)",
                    static_cast<long long>(std::abs(mSelection.cornerB().x - mSelection.cornerA().x) + 1),
                    static_cast<long long>(std::abs(mSelection.cornerB().y - mSelection.cornerA().y) + 1),
                    static_cast<long long>(std::abs(mSelection.cornerB().z - mSelection.cornerA().z) + 1),
                    mSelection.volume());
      setStatus(buffer);
      return;
    }
    case ToolMode::SchematicPlacement: {
      if (!hit.hit) {
        setStatus("Schematic Placement: look at a block");
        return;
      }
      mPlacement.origin = bitematica::litematica::BlockTargeting::placementAnchor(hit);
      mPlacement.yOffset = 0;
      refreshRenderSnapshot();
      rebuildMeshAsync();
      refreshStatusLine();
      return;
    }
    case ToolMode::Paste: {
      if (!mHasSchematic) {
        setStatus("Paste: nothing loaded");
        return;
      }
      const auto commands = bitematica::litematica::PasteJob::build(schematic(), mPlacement);
      const std::filesystem::path exports = mSelf->getDataDir() / "exports";
      std::error_code ec;
      std::filesystem::create_directories(exports, ec);
      // Two files: the copyable command list (with the leading slash) and an .mcfunction body
      // (H7) a function pack can run with one `/function`.
      {
        std::ofstream out(exports / "paste_commands.txt", std::ios::trunc);
        if (out) {
          for (const std::string &command : commands) out << "/" << command << "\n";
        }
      }
      {
        std::ofstream out(exports / "paste.mcfunction", std::ios::trunc);
        if (out) out << bitematica::litematica::PasteJob::mcfunctionText(commands);
      }
      // H8: the overwrite count, so the user knows what the paste would replace. Nothing is placed
      // here; the commands are files the player chooses to run.
      long long overwrites = 0;
      if (mWorldSource.available()) {
        overwrites = bitematica::litematica::PasteJob::overwriteCount(
            schematic(), mPlacement, [this](const BlockPos &p) {
              BlockState state;
              return mWorldSource.readBlock(p, state) && !state.isAir();
            });
      }
      char buffer[192];
      std::snprintf(buffer, sizeof(buffer),
                    "Paste: %zu command(s) written to exports (would overwrite %lld block(s))",
                    commands.size(), overwrites);
      setStatus(buffer);
      return;
    }
    case ToolMode::Rebuild: {
      runVerification();
      return;
    }
    case ToolMode::PickBlock: {
      if (!hit.hit) {
        setStatus("Pick Block: look at a block");
        return;
      }
      bitematica::core::LocalPos local{};
      if (mPlacement.toLocal(schematic(), hit.block, local)) {
        const BlockState &state = schematic().stateAt(local.x, local.y, local.z);
        setStatus("Pick Block: " + state.baseName());
      } else {
        setStatus("Pick Block: no schematic block there");
      }
      return;
    }
    case ToolMode::Fill:
    case ToolMode::Delete:
    case ToolMode::Move:
    case ToolMode::Grid: {
      // These write to the world and are only meaningful in creative with cheats. They report what
      // they would do rather than acting, so nothing is changed without the player's command.
      setStatus(std::string(bitematica::litematica::toolModeName(mToolMode)) +
                ": use the paste commands (exports/paste_commands.txt) in creative");
      return;
    }
    case ToolMode::Count:
      return;
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
  // Rows come from the live list when a verification has run, otherwise the static list. The same
  // rows drive both exports (I6): CSV and a plain-text shopping list.
  std::vector<bitematica::core::LiveMaterialRow> rows = mLiveMaterials;
  if (rows.empty()) {
    for (const auto &entry : mMaterials) {
      rows.push_back({entry.name, static_cast<long long>(entry.count), 0,
                      static_cast<long long>(entry.count)});
    }
  }
  {
    std::ofstream out(exports / "materials.csv", std::ios::trunc);
    if (!out) {
      setStatus("Could not write the CSV");
      return;
    }
    out << bitematica::core::LiveMaterials::toCsv(rows);
  }
  {
    std::ofstream out(exports / "materials.txt", std::ios::trunc);
    if (out) out << bitematica::core::LiveMaterials::toText(rows);
  }
  setStatus("Materials written to " + (exports / "materials.csv").string());
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
                                                                                     : "translucent");
  const char *layer = mConfig.layerMode == bitematica::mod::LayerMode::All
                          ? "all"
                          : (mConfig.layerMode == bitematica::mod::LayerMode::Single ? "layer"
                                                                                     : "range");
  // The status line always names the render mode, so the user is never left guessing whether the
  // schematic is being drawn in world space. When it is not, the reason is spelled out and nothing
  // is drawn — there is no screen-space fallback.
  // A one-time render failure (backend unusable / program would not link) takes precedence over the
  // seam name: it is the more specific reason and it is why nothing is on screen.
  char renderState[200];
  if (mRenderErrorSet.load(std::memory_order_acquire)) {
    std::lock_guard<std::mutex> lock(mRenderErrorMutex);
    std::snprintf(renderState, sizeof(renderState), "%s", mRenderError.c_str());
  } else if (mRenderSeam == RenderSeam::Preloader) {
    std::snprintf(renderState, sizeof(renderState),
                  "World-space GL (sdk, frame %llu, %d sections, %d tris)",
                  mFramesDrawn.load(std::memory_order_relaxed), mRenderer.lastDrawnSections(),
                  mRenderer.lastDrawnTriangles());
  } else if (mRenderSeam == RenderSeam::Egl) {
    std::snprintf(renderState, sizeof(renderState),
                  "World-space GL (egl, frame %llu, %d sections, %d tris)",
                  mFramesDrawn.load(std::memory_order_relaxed), mRenderer.lastDrawnSections(),
                  mRenderer.lastDrawnTriangles());
  } else {
    std::snprintf(renderState, sizeof(renderState),
                  "World-space renderer unavailable: no render seam - update GlowberryClient");
  }

  char buffer[420];
  std::snprintf(buffer, sizeof(buffer), "%s  %dx%dx%d  %s  %s  rot %d  @%d,%d,%d  %s  cam:%s",
                schematic().name.c_str(), schematic().sizeX(), schematic().sizeY(),
                schematic().sizeZ(), mode, layer, static_cast<int>(mPlacement.rotation) * 90,
                mPlacement.origin.x, mPlacement.origin.y, mPlacement.origin.z, renderState,
                bitematica::mod::LocalPlayerFeed::diagnostic());
  setStatus(buffer);
}

void BiteMaticaMod::setStatus(const std::string &message) { mStatus = message; }

void BiteMaticaMod::submitHud() {
  // Every input-driven change funnels through here, so this is where the render thread's copy of
  // the placement and config is refreshed, and where the menu pages are rebuilt to match.
  refreshRenderSnapshot();
  rebuildMenuPages();
  mButtons = HudOverlay::layoutToolbar(mConfig);

  // The material panel shows the live list when a verification has run, otherwise the static one.
  std::vector<bitematica::core::MaterialEntry> displayMaterials = mMaterials;
  if (!mLiveMaterials.empty()) {
    displayMaterials.clear();
    for (const auto &row : mLiveMaterials) {
      displayMaterials.push_back({row.name, static_cast<uint64_t>(row.remaining())});
    }
  }

  std::string infoLine = mHint.empty() ? mStatus : (mStatus + "   " + mHint);
  // Chrome only. The schematic is never drawn as HUD geometry: when no world-space seam is
  // available the status line names the reason and nothing is drawn (see refreshStatusLine).
  std::vector<UiCommand> commands =
      HudOverlay::buildCommands(mConfig, mButtons, displayMaterials, infoLine, mHasSchematic);

  // The floating menu icon is always drawn, so the menu is reachable with a tap and never needs a
  // keyboard or the Mod Menu. It is anchored to the top-left of the screen rather than relative to
  // the toolbar, so it stays on screen whatever the toolbar anchor is set to and cannot be pushed
  // off the edge by a small viewport.
  const float iconSize = 52.0f;
  const float iconX = 12.0f;
  const float iconY = 12.0f;
  const auto icon = bitematica::ui::MenuRenderer::layoutIcon(iconX, iconY, iconSize, mMenuOpen);
  mMenuIconX = iconX;
  mMenuIconY = iconY;
  mMenuIconSize = iconSize;
  commands.insert(commands.end(), icon.commands.begin(), icon.commands.end());

  // When the menu is open, lay out the active page and append it. Its hitboxes are stashed so a tap
  // can be matched without re-deriving the layout.
  mMenuHits.clear();
  if (mMenuOpen && !mMenuPages.empty()) {
    const int tab = std::clamp(mMenuTab, 0, static_cast<int>(mMenuPages.size()) - 1);
    const auto layout = bitematica::ui::MenuRenderer::layout(
        mMenuPages[static_cast<size_t>(tab)], tab, static_cast<int>(mMenuPages.size()),
        mMenuScroll);
    commands.insert(commands.end(), layout.commands.begin(), layout.commands.end());
    mMenuHits = layout.hits;
    mMenuPanel = {layout.panelX, layout.panelY, layout.panelW, layout.panelH};
  }

  std::vector<pl::modmenu::DrawCommand> sdkCommands;
  sdkCommands.reserve(commands.size());
  for (const UiCommand &command : commands) {
    pl::modmenu::DrawCommand out{};
    out.type = toSdkKind(command.kind);
    out.x = command.x;
    out.y = command.y;
    out.w = command.w;
    out.h = command.h;
    out.color = command.color;
    out.size = command.size;
    out.text = command.text;
    sdkCommands.push_back(std::move(out));
  }
  pl::modmenu::submitDrawCommands(kModuleId, sdkCommands);
}

void BiteMaticaMod::rebuildMenuPages() {
  // The menu reflects the live state: the config, the placements, the material counts and the
  // loaded files. Rebuilding it on every change keeps the two from drifting.
  bitematica::ui::MenuBuilder::Context context;
  context.config = &mConfig;
  context.placements = &mPlacements;
  context.toolMode = mToolMode;
  context.layers.mode = static_cast<bitematica::litematica::LayerMode>(
      static_cast<int>(mConfig.layerMode));
  context.layers.layer = mConfig.layerSingle;
  context.layers.layerMax = mConfig.layerMax;
  context.statusLine = mStatus;
  context.worldSpace = worldSpaceActive();
  context.worldReadable = mWorldSource.available();
  for (const auto &row : mLiveMaterials) {
    context.materials.emplace_back(row.name, row.required, row.placed, row.missing);
  }
  if (context.materials.empty()) {
    for (const auto &entry : mMaterials) {
      context.materials.emplace_back(entry.name, static_cast<long long>(entry.count), 0,
                                     static_cast<long long>(entry.count));
    }
  }
  for (const auto &file : mFiles) {
    context.loadedNames.push_back(std::filesystem::path(file).filename().string());
  }
  if (!mFiles.empty()) {
    context.tasks.push_back("mesh build (worker)");
  }

  mMenuPages = bitematica::ui::MenuBuilder::buildAll(context);
}

void BiteMaticaMod::activateWidget(bitematica::ui::Widget &widget) {
  // A tap on a row changes it. Toggles flip, choices cycle, sliders step by 10% of their range, and
  // buttons fire. Everything writes back through the config so the change persists.
  switch (widget.kind) {
    case bitematica::ui::WidgetKind::Toggle: {
      const bool on = widget.value == "true";
      widget.value = on ? "false" : "true";
      applyMenuConfig(widget.id, widget.value);
      return;
    }
    case bitematica::ui::WidgetKind::Choice: {
      int index = 0;
      for (char c : widget.value) {
        if (c >= '0' && c <= '9') index = index * 10 + (c - '0');
      }
      index = (index + 1) % static_cast<int>(widget.choices.size());
      widget.value = std::to_string(index);
      applyMenuConfig(widget.id, widget.value);
      return;
    }
    case bitematica::ui::WidgetKind::Slider: {
      int value = 0;
      for (char c : widget.value) {
        if (c >= '0' && c <= '9') value = value * 10 + (c - '0');
      }
      const int step = std::max(1, (widget.maxValue - widget.minValue) / 10);
      value += step;
      if (value > widget.maxValue) value = widget.minValue;
      widget.value = std::to_string(value);
      applyMenuConfig(widget.id, widget.value);
      return;
    }
    case bitematica::ui::WidgetKind::Button:
      applyMenuAction(widget.id);
      return;
    default:
      return;
  }
}

bool BiteMaticaMod::handleMenuTouch(float x, float y) {
  // The icon toggles the menu and is checked first, so it works whether the menu is open or closed.
  if (x >= mMenuIconX && x <= mMenuIconX + mMenuIconSize && y >= mMenuIconY &&
      y <= mMenuIconY + mMenuIconSize) {
    mMenuOpen = !mMenuOpen;
    mMenuScroll = 0;
    rebuildMenuPages();
    submitHud();
    return true;
  }
  if (!mMenuOpen) return false;

  // A tap inside the panel is matched to a tab, a row or the close button.
  for (const auto &hit : mMenuHits) {
    if (x < hit.x || x > hit.x + hit.w || y < hit.y || y > hit.y + hit.h) continue;
    switch (hit.kind) {
      case bitematica::ui::MenuHit::Kind::Close:
        mMenuOpen = false;
        submitHud();
        return true;
      case bitematica::ui::MenuHit::Kind::Tab:
        mMenuTab = hit.tab;
        mMenuScroll = 0;
        submitHud();
        return true;
      case bitematica::ui::MenuHit::Kind::Widget: {
        if (hit.widgetIndex < 0 ||
            hit.widgetIndex >= static_cast<int>(mMenuPages.size()) ||
            mMenuTab < 0 || mMenuTab >= static_cast<int>(mMenuPages.size())) {
          return true;
        }
        auto &page = mMenuPages[static_cast<size_t>(mMenuTab)];
        if (hit.widgetIndex < static_cast<int>(page.widgets.size())) {
          activateWidget(page.widgets[static_cast<size_t>(hit.widgetIndex)]);
          rebuildMenuPages();
          submitHud();
        }
        return true;
      }
    }
  }

  // A tap outside the panel closes the menu, so it never traps the player.
  if (x < mMenuPanel.x || x > mMenuPanel.x + mMenuPanel.w || y < mMenuPanel.y ||
      y > mMenuPanel.y + mMenuPanel.h) {
    mMenuOpen = false;
    submitHud();
    return true;
  }
  return true; // swallow taps on the panel background
}

void BiteMaticaMod::applyMenuConfig(const std::string &id, const std::string &value) {
  const int number = std::atoi(value.c_str());
  const bool flag = value == "true";

  if (id == "enabled") mConfig.enabled = flag;
  else if (id == "showMaterialList") mConfig.showMaterialList = flag;
  else if (id == "showToolbar") mConfig.showToolbar = flag;
  else if (id == "ghostPlacement") mConfig.ghostPlacement = flag;
  else if (id == "onlyExposed") mConfig.onlyExposed = flag;
  else if (id == "verifyAgainstWorld") mConfig.verifyAgainstWorld = flag;
  else if (id == "verifyExtras") mConfig.verifyExtras = flag;
  else if (id == "renderMode") mConfig.renderMode = static_cast<bitematica::mod::RenderMode>(number);
  else if (id == "opacity") mConfig.opacity = number;
  else if (id == "layerMode") mConfig.layerMode = static_cast<bitematica::mod::LayerMode>(number);
  else if (id == "layerSingle") mConfig.layerSingle = number;
  else if (id == "layerMin") mConfig.layerMin = number;
  else if (id == "layerMax") mConfig.layerMax = number;
  else if (id == "maxTriangles") mConfig.maxTriangles = number;
  else if (id == "renderDistance") mConfig.renderDistance = number;
  else if (id == "maxRenderBlocks") mConfig.maxRenderBlocks = number;

  mConfig.normalize();
  saveConfig();
  // A layer or mode change alters what is drawn, so the mesh and the status line are refreshed.
  if (id == "layerMode" || id == "layerSingle" || id == "layerMin" || id == "layerMax") {
    rebuildMeshAsync();
  }
  refreshStatusLine();
}

void BiteMaticaMod::applyMenuAction(const std::string &id) {
  // Menu buttons reuse the toolbar's actions wherever the behaviour is the same, so there is one
  // implementation per operation.
  using Action = bitematica::render::ToolbarButton::Action;
  if (id == "main.nextmode" || id == "placement.rotateCw") {
    performAction(id == "main.nextmode" ? Action::CycleToolMode : Action::RotatePlacement);
  } else if (id == "main.prevmode") {
    mToolMode = bitematica::litematica::previousToolMode(mToolMode);
  } else if (id == "placement.rotateCcw") {
    performAction(Action::RotateLeft);
  } else if (id == "placement.mirror") {
    performAction(Action::CycleMirror);
  } else if (id == "placement.moveToPlayer") {
    performAction(Action::MoveToPlayer);
  } else if (id == "placement.moveUp") {
    performAction(Action::NudgeUp);
  } else if (id == "placement.moveDown") {
    performAction(Action::NudgeDown);
  } else if (id == "placement.next") {
    mPlacements.stepActive(1);
  } else if (id == "placement.prev") {
    mPlacements.stepActive(-1);
  } else if (id == "materials.exportCsv") {
    performAction(Action::ExportCsv);
  } else if (id == "layer.stepUp") {
    int lo = 0;
    int hi = 0;
    currentLayerRange(lo, hi);
    mConfig.layerSingle = std::min(mConfig.layerSingle + 1, std::max(0, schematic().sizeY() - 1));
    mConfig.layerMode = bitematica::mod::LayerMode::Single;
    saveConfig();
    rebuildMeshAsync();
  } else if (id == "layer.stepDown") {
    mConfig.layerSingle = std::max(0, mConfig.layerSingle - 1);
    mConfig.layerMode = bitematica::mod::LayerMode::Single;
    saveConfig();
    rebuildMeshAsync();
  } else if (id == "area.setCorner1" || id == "area.setCorner2" || id == "area.paste") {
    applyTool();
  } else if (id == "area.clear") {
    mSelection.clear();
    setStatus("Selection cleared");
  } else if (id == "area.saveLitematic") {
    performAction(Action::SaveLitematic);
  } else if (id == "area.saveStructure") {
    performAction(Action::ExportStructure);
  } else if (id == "loaded.reload") {
    performAction(Action::Reload);
  } else if (id == "loaded.unload") {
    mPlacements.clear();
    mHasSchematic = false;
    mSchematicPtr.reset();
    setStatus("Unloaded");
  }
  refreshStatusLine();
  submitHud();
}

void BiteMaticaMod::saveConfig() {
  if (mSelf == nullptr) return;
  std::ofstream out(mSelf->getConfigDir() / "config.json", std::ios::trunc);
  if (out) out << bitematica::mod::JsonConfig::serialize(mConfig);
}

} // namespace

PL_REGISTER_MOD(BiteMaticaMod, BiteMaticaMod::instance())
