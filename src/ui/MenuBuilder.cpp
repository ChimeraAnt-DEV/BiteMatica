#include "ui/MenuBuilder.hpp"

#include <cstdio>

namespace bitematica::ui {
namespace {

Widget header(const std::string &label) {
  Widget w;
  w.kind = WidgetKind::Header;
  w.id = "h:" + label;
  w.label = label;
  return w;
}

Widget info(const std::string &id, const std::string &label, const std::string &value) {
  Widget w;
  w.kind = WidgetKind::Text;
  w.id = id;
  w.label = label;
  w.value = value;
  return w;
}

Widget toggle(const std::string &id, const std::string &label, bool value,
              const std::string &description = {}) {
  Widget w;
  w.kind = WidgetKind::Toggle;
  w.id = id;
  w.label = label;
  w.value = value ? "true" : "false";
  w.description = description;
  return w;
}

Widget slider(const std::string &id, const std::string &label, int value, int minValue, int maxValue,
              const std::string &description = {}) {
  Widget w;
  w.kind = WidgetKind::Slider;
  w.id = id;
  w.label = label;
  w.value = std::to_string(value);
  w.minValue = minValue;
  w.maxValue = maxValue;
  w.description = description;
  return w;
}

Widget choice(const std::string &id, const std::string &label, int value,
              std::vector<std::string> options, const std::string &description = {}) {
  Widget w;
  w.kind = WidgetKind::Choice;
  w.id = id;
  w.label = label;
  w.value = std::to_string(value);
  w.choices = std::move(options);
  w.description = description;
  return w;
}

Widget button(const std::string &id, const std::string &label) {
  Widget w;
  w.kind = WidgetKind::Button;
  w.id = id;
  w.label = label;
  return w;
}

std::string yesNo(bool value) { return value ? "yes" : "no"; }

} // namespace

MenuPage MenuBuilder::buildMain(const Context &context) {
  MenuPage page;
  page.tab = MenuTab::Main;
  page.title = "BiteMatica";

  page.widgets.push_back(header("Tool"));
  page.widgets.push_back(info("main.toolmode", "Tool Mode",
                              litematica::toolModeName(context.toolMode)));
  page.widgets.push_back(info("main.toolhint", "Hint", litematica::toolModeHint(context.toolMode)));
  page.widgets.push_back(button("main.prevmode", "Previous mode"));
  page.widgets.push_back(button("main.nextmode", "Next mode"));

  page.widgets.push_back(header("Status"));
  page.widgets.push_back(info("main.seam", "Rendering",
                              context.worldSpace ? "world-space (GL)" : "software overlay"));
  page.widgets.push_back(info("main.verify", "World readable", yesNo(context.worldReadable)));
  page.widgets.push_back(info("main.status", "Status", context.statusLine));
  page.widgets.push_back(
      info("main.placements", "Placements",
           std::to_string(context.placements ? context.placements->count() : 0)));

  page.widgets.push_back(header("Quick Toggles"));
  const mod::Config &config = *context.config;
  page.widgets.push_back(toggle("enabled", "Enabled", config.enabled));
  page.widgets.push_back(toggle("showMaterialList", "Material List", config.showMaterialList));
  page.widgets.push_back(toggle("showToolbar", "Toolbar", config.showToolbar));
  page.widgets.push_back(toggle("ghostPlacement", "Ghost Placement", config.ghostPlacement));
  return page;
}

MenuPage MenuBuilder::buildConfig(const Context &context) {
  MenuPage page;
  page.tab = MenuTab::Config;
  page.title = "Configuration";
  const mod::Config &config = *context.config;

  page.widgets.push_back(header("Generic"));
  page.widgets.push_back(toggle("enabled", "Enabled", config.enabled));
  page.widgets.push_back(toggle("verifyAgainstWorld", "Verify Against World",
                                config.verifyAgainstWorld,
                                "Colour blocks by whether the world matches"));
  page.widgets.push_back(toggle("verifyExtras", "Report Extra Blocks", config.verifyExtras));

  page.widgets.push_back(header("Visuals"));
  page.widgets.push_back(choice("renderMode", "Render Mode",
                                static_cast<int>(config.renderMode),
                                {"Overlay", "Skeleton", "Hidden"}));
  page.widgets.push_back(slider("opacity", "Opacity", config.opacity, 10, 100));
  page.widgets.push_back(toggle("onlyExposed", "Exposed Faces Only", config.onlyExposed));
  page.widgets.push_back(toggle("showMaterialList", "Material List", config.showMaterialList));
  page.widgets.push_back(toggle("showToolbar", "Toolbar", config.showToolbar));
  page.widgets.push_back(toggle("ghostPlacement", "Ghost Placement", config.ghostPlacement));

  page.widgets.push_back(header("Colors"));
  // The colour set is fixed for now; the page exists so the schema matches Litematica's structure
  // and a colour picker can be added without a new page.
  page.widgets.push_back(info("color.missing", "Missing", "#FF4040"));
  page.widgets.push_back(info("color.wrong", "Wrong", "#FFA030"));
  page.widgets.push_back(info("color.extra", "Extra", "#4080FF"));
  page.widgets.push_back(info("color.correct", "Correct", "#00C060"));

  page.widgets.push_back(header("Performance"));
  page.widgets.push_back(slider("maxTriangles", "Triangle Budget", config.maxTriangles, 1000,
                                4000000));
  page.widgets.push_back(slider("renderDistance", "Render Distance", config.renderDistance, 0,
                                1024));
  page.widgets.push_back(slider("maxRenderBlocks", "Render Block Cap", config.maxRenderBlocks, 500,
                                250000));

  page.widgets.push_back(header("Hotkeys"));
  page.widgets.push_back(info("hotkey.menu", "Menu", "Mod Menu -> BiteMatica"));
  page.widgets.push_back(info("hotkey.cycle", "Cycle tool mode", "L1 / R1 (controller)"));
  page.widgets.push_back(info("hotkey.place", "Place / act", "A (controller), tap (touch)"));

  page.widgets.push_back(header("Info Switches"));
  page.widgets.push_back(info("info.mode", "Show mode", "yes"));
  page.widgets.push_back(info("info.coords", "Show placement origin", "yes"));
  page.widgets.push_back(info("info.materials", "Show material totals", "yes"));
  return page;
}

MenuPage MenuBuilder::buildMaterials(const Context &context) {
  MenuPage page;
  page.tab = MenuTab::Materials;
  page.title = "Material List";
  page.widgets.push_back(header("Totals"));
  page.widgets.push_back(button("materials.exportCsv", "Export CSV"));

  long long totalRequired = 0;
  long long totalPlaced = 0;
  long long totalMissing = 0;
  for (const auto &[name, required, placed, missing] : context.materials) {
    (void)name;
    totalRequired += required;
    totalPlaced += placed;
    totalMissing += missing;
  }
  page.widgets.push_back(info("materials.required", "Total Needed", std::to_string(totalRequired)));
  page.widgets.push_back(info("materials.placed", "Placed", std::to_string(totalPlaced)));
  page.widgets.push_back(info("materials.missing", "Missing", std::to_string(totalMissing)));

  page.widgets.push_back(header("Blocks"));
  for (const auto &[name, required, placed, missing] : context.materials) {
    char buffer[160];
    std::snprintf(buffer, sizeof(buffer), "%lld / %lld  (%lld left)", placed, required, missing);
    page.widgets.push_back(info("material." + name, name, buffer));
  }
  return page;
}

MenuPage MenuBuilder::buildPlacements(const Context &context) {
  MenuPage page;
  page.tab = MenuTab::Placements;
  page.title = "Schematic Placements";

  const litematica::PlacementManager *manager = context.placements;
  if (manager == nullptr || manager->empty()) {
    page.widgets.push_back(info("placements.none", "Placements", "none loaded"));
    return page;
  }

  const litematica::PlacedSchematic *active = manager->active();
  page.widgets.push_back(header("Active"));
  if (active != nullptr) {
    page.widgets.push_back(info("placement.name", "Name", active->name()));
    page.widgets.push_back(info("placement.size", "Size",
                                std::to_string(active->sizeX()) + "x" +
                                    std::to_string(active->sizeY()) + "x" +
                                    std::to_string(active->sizeZ())));
    page.widgets.push_back(info("placement.origin", "Origin",
                                std::to_string(active->placement.origin.x) + ", " +
                                    std::to_string(active->placement.origin.y) + ", " +
                                    std::to_string(active->placement.origin.z)));
    page.widgets.push_back(toggle("placement.visible", "Visible", active->visible));
    page.widgets.push_back(toggle("placement.locked", "Locked (pin origin)", active->locked));
    page.widgets.push_back(button("placement.rotateCw", "Rotate 90 CW"));
    page.widgets.push_back(button("placement.rotateCcw", "Rotate 90 CCW"));
    page.widgets.push_back(button("placement.mirror", "Cycle Mirror"));
    page.widgets.push_back(button("placement.moveToPlayer", "Move to Player"));
    page.widgets.push_back(button("placement.moveUp", "Y +1"));
    page.widgets.push_back(button("placement.moveDown", "Y -1"));

    page.widgets.push_back(header("Sub-regions"));
    for (size_t i = 0; i < active->subRegionVisible.size(); ++i) {
      page.widgets.push_back(toggle("placement.sub." + std::to_string(i),
                                    "Region " + std::to_string(i),
                                    active->subRegionVisible[i]));
    }
  }

  page.widgets.push_back(header("All Placements"));
  for (int i = 0; i < manager->count(); ++i) {
    const litematica::PlacedSchematic *entry = manager->at(i);
    page.widgets.push_back(info("placement.list." + std::to_string(i),
                                std::to_string(i) + (i == manager->activeIndex() ? " *" : ""),
                                entry->name()));
  }
  page.widgets.push_back(button("placement.next", "Next placement"));
  page.widgets.push_back(button("placement.prev", "Previous placement"));
  return page;
}

MenuPage MenuBuilder::buildRender(const Context &context) {
  MenuPage page;
  page.tab = MenuTab::Render;
  page.title = "Render Layers";
  const mod::Config &config = *context.config;

  page.widgets.push_back(header("Layer Mode"));
  page.widgets.push_back(choice("layerMode", "Mode", static_cast<int>(config.layerMode),
                                {"All", "Single Layer", "Range"}));
  page.widgets.push_back(slider("layerSingle", "Layer", config.layerSingle, 0, 255));
  page.widgets.push_back(slider("layerMin", "Range Start", config.layerMin, 0, 255));
  page.widgets.push_back(slider("layerMax", "Range End", config.layerMax, 0, 255));
  page.widgets.push_back(button("layer.stepUp", "Step Layer Up"));
  page.widgets.push_back(button("layer.stepDown", "Step Layer Down"));

  page.widgets.push_back(header("Draw"));
  page.widgets.push_back(choice("renderMode", "Render Mode", static_cast<int>(config.renderMode),
                                {"Overlay", "Skeleton", "Hidden"}));
  page.widgets.push_back(slider("opacity", "Opacity", config.opacity, 10, 100));
  page.widgets.push_back(slider("renderDistance", "Render Distance", config.renderDistance, 0,
                                1024));
  return page;
}

MenuPage MenuBuilder::buildAreaEditor(const Context &context) {
  MenuPage page;
  page.tab = MenuTab::AreaEditor;
  page.title = "Area Editor";
  page.widgets.push_back(header("Selection"));
  page.widgets.push_back(button("area.setCorner1", "Set Corner 1 (at crosshair)"));
  page.widgets.push_back(button("area.setCorner2", "Set Corner 2 (at crosshair)"));
  page.widgets.push_back(button("area.expand", "Expand along view"));
  page.widgets.push_back(button("area.clear", "Clear selection"));
  page.widgets.push_back(info("area.status", "Selection", context.statusLine));

  page.widgets.push_back(header("Save"));
  page.widgets.push_back(button("area.saveLitematic", "Save as .litematic"));
  page.widgets.push_back(button("area.saveStructure", "Export .mcstructure"));
  page.widgets.push_back(button("area.paste", "Paste into world"));
  return page;
}

MenuPage MenuBuilder::buildLoaded(const Context &context) {
  MenuPage page;
  page.tab = MenuTab::Loaded;
  page.title = "Loaded Schematics";
  page.widgets.push_back(header("In memory"));
  if (context.loadedNames.empty()) {
    page.widgets.push_back(info("loaded.none", "Loaded", "none"));
  }
  for (size_t i = 0; i < context.loadedNames.size(); ++i) {
    page.widgets.push_back(
        info("loaded." + std::to_string(i), std::to_string(i), context.loadedNames[i]));
  }
  page.widgets.push_back(button("loaded.reload", "Reload folder"));
  page.widgets.push_back(button("loaded.unload", "Unload active"));
  return page;
}

MenuPage MenuBuilder::buildTasks(const Context &context) {
  MenuPage page;
  page.tab = MenuTab::Tasks;
  page.title = "Task Manager";
  page.widgets.push_back(header("Background"));
  if (context.tasks.empty()) {
    page.widgets.push_back(info("tasks.idle", "Queue", "idle"));
  }
  for (size_t i = 0; i < context.tasks.size(); ++i) {
    page.widgets.push_back(info("task." + std::to_string(i), "Task", context.tasks[i]));
  }
  return page;
}

std::vector<MenuPage> MenuBuilder::buildAll(const Context &context) {
  std::vector<MenuPage> pages;
  pages.push_back(buildMain(context));
  pages.push_back(buildConfig(context));
  pages.push_back(buildMaterials(context));
  pages.push_back(buildPlacements(context));
  pages.push_back(buildRender(context));
  pages.push_back(buildAreaEditor(context));
  pages.push_back(buildLoaded(context));
  pages.push_back(buildTasks(context));
  return pages;
}

} // namespace bitematica::ui
