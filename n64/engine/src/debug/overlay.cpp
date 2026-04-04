/**
* @copyright 2024 - Max Bebök
* @license MIT
*/
#include "overlay.h"

#include "debug/debugDraw.h"
#include "debug/debugMenu.h"
#include "scene/scene.h"
#include "vi/swapChain.h"
#include "audio/audioManager.h"
#include "lib/matrixManager.h"
#include "lib/memory.h"
#include "lib/hash.h"

#include <vector>
#include <string>
#include <variant>
#include <filesystem>

#include "../audio/audioManagerPrivate.h"

namespace P64::SceneManager
{
  extern const char* SCENE_NAMES[];
}

// ==============================================
// Tree-based Menu system (single root)
// ==============================================

// Generate MenuItemType from the exact same list as DebugVarType
#define X(Type, Name) Name,
enum class MenuItemType : uint8_t {
    DEBUG_MENU_TYPES
    ACTION,
    BACK
};
#undef X

struct MenuItem {
    std::string text{};
    Debug::Menu::MenuValue value{};
    Debug::Menu::MenuValue step{};
    MenuItemType type = MenuItemType::ACTION;
    std::function<void(MenuItem&)> onChange;
};

struct Menu {
    std::string title = "Debug";
    Menu* parent = nullptr;
    std::vector<MenuItem> items{};
    std::vector<Menu> children{};
    uint32_t currIndex = 0;
};

namespace {
  constexpr uint32_t SCREEN_HEIGHT = 240;
  constexpr uint32_t SCREEN_WIDTH = 320;

  constexpr float barWidth = 280.0f;
  constexpr float barHeight = 3.0f;
  constexpr float barRefTimeMs = 1000.0f / 30.0f; // FPS

  constexpr color_t COLOR_BVH{ 0x00, 0xAA, 0x22, 0xFF};
  constexpr color_t COLOR_COLL{0x22,0xFF,0x00, 0xFF};
  constexpr color_t COLOR_ACTOR_UPDATE{0xAA,0,0, 0xFF};
  constexpr color_t COLOR_GLOBAL_UPDATE{0x33,0x33,0x33, 0xFF};
  constexpr color_t COLOR_SCENE_DRAW{0xFF,0x80,0x10, 0xFF};
  constexpr color_t COLOR_GLOBAL_DRAW{0x33,0x33,0x33, 0xFF};
  constexpr color_t COLOR_AUDIO{0x43, 0x52, 0xFF, 0xFF};

  constexpr const char* STR_DEBUG    = "Debug";
  constexpr const char* STR_SCENES   = "Scenes";
  constexpr const char* STR_BACK     = "< Back >";
  constexpr const char* STR_CH_TODO  = "CH (TODO)";

  constinit Menu menu{};
  Menu* currentMenu = nullptr;

  constinit T3DMetrics *metrics = nullptr;
  
  uint64_t ticksSelf = 0;

  constexpr float usToWidth(long timeUs) {
    double timeMs = (double)timeUs / 1000.0;
    return (float)(timeMs / (double)barRefTimeMs) * barWidth;
  }

  float frameTimeScale = 2;

  std::vector<std::string> sceneNames{};

  float ticksToOffset(uint32_t ticks) {
    float timeOffX = TICKS_TO_US((uint64_t)ticks) / 1000.0f;
    return timeOffX * frameTimeScale;
  };

  // -------------------------------------------------------------------------
  // Helper: create the correct submenu tree from a | separated path
  //         AND automatically add "< Back >" to every non-root submenu
  // -------------------------------------------------------------------------
  Menu* findOrCreateSubmenu(Menu& root, std::string path)
  {
    Menu* current = &root;
    size_t pos = 0;

    while ((pos = path.find('|')) != std::string::npos)
    {
      std::string submenuName = path.substr(0, pos);
      path.erase(0, pos + 1);

      bool found = false;
      for (auto& child : current->children)
      {
        if (child.title == submenuName)
        {
          current = &child;
          found = true;
          break;
        }
      }

      if (!found)
      {
        current->children.emplace_back();
        Menu& newMenu = current->children.back();
        newMenu.title = submenuName;
        newMenu.parent = current;
        current = &newMenu;
      }
    }

    return current;
  }

  void addRegisteredDebugVarsToMenu(Menu& m)
  {
    for (const auto& var : Debug::Menu::DebugRegistry::Get().GetAllVars())
    {
      Menu* targetMenu = findOrCreateSubmenu(m, var.path);

      // Extract only the final leaf name for display (full path is kept in the submenu hierarchy)
      std::string fullPath = var.path;
      size_t lastPipe = fullPath.find_last_of('|');
      std::string displayName = (lastPipe == std::string::npos)
                                ? fullPath
                                : fullPath.substr(lastPipe + 1);

      #define X(Type, Name) \
      if (var.type == Debug::Menu::DebugVarType::Name) { \
        Type* ptr = static_cast<Type*>(var.ptr); \
        targetMenu->items.push_back({ \
          displayName, \
          Debug::Menu::MenuValue{*ptr}, \
          var.step, \
          MenuItemType::Name, \
          [ptr](MenuItem& item) { *ptr = std::get<Type>(item.value); } \
        }); \
      }
      DEBUG_MENU_TYPES
      #undef X
    }
  }

  void addActionItem(Menu &m, const char* name, std::function<void(MenuItem&)> action) {
    m.items.push_back({std::string(name), Debug::Menu::MenuValue{}, Debug::Menu::MenuValue{}, MenuItemType::ACTION, action});
  }

  void addBackToMenu(Menu& menuFocus)
  {
    // the back item should always be first in the list
    if(menuFocus.parent != nullptr 
    &&(menuFocus.items.size() == 0 || menuFocus.items[0].type != MenuItemType::BACK))
    {
        menuFocus.items.insert(menuFocus.items.begin(), {
            STR_BACK,
            Debug::Menu::MenuValue{},
            Debug::Menu::MenuValue{},
            MenuItemType::BACK,
            [](MenuItem&) {}
        });
    }

    for(Menu& child : menuFocus.children)
    {
        addBackToMenu(child);
    }
  }

  REGISTER_TWEAKABLE_VAR(bool, "Coll-Obj",   showCollBCS,   false);
  REGISTER_TWEAKABLE_VAR(bool, "Coll-Tri",   showCollMesh,  false);
  REGISTER_TWEAKABLE_VAR(bool, "Memory",     matrixDebug,   false);
  REGISTER_TWEAKABLE_VAR(bool, "Frames",     showFrameTime, false);
  REGISTER_TWEAKABLE_VAR(bool, "FPS",        showFrameRate, false);
  REGISTER_TWEAKABLE_VAR(bool, "Debug Shapes", showDebugDraws, false);

  bool isVisible = false;
  bool didInit = false;
  bool isVarDirty = false;
}

void Debug::Overlay::toggle()
{
  isVisible = !isVisible;
}

namespace fs = std::filesystem;

void Debug::Overlay::init()
{
  sceneNames = {};

  dir_t dir{};
  const char* const BASE_DIR = "rom:/p64";
  int res = dir_findfirst(BASE_DIR, &dir);
  while(res == 0)
  {
    std::string name{dir.d_name};
    if(name[0] == 's' && name.length() == 5) {
      auto id = std::stoi(name.substr(1));
      sceneNames.push_back(name.substr(1) + " - " + P64::SceneManager::SCENE_NAMES[id-1]);
    }
    res = dir_findnext(BASE_DIR, &dir);
  }
}

static void buildDebugMenu()
{
  menu.title = STR_DEBUG;
  menu.parent = nullptr;
  menu.currIndex = 0;

  // Scenes child menu should be above the variables
  menu.children.emplace_back();
  Menu& scenesMenu = menu.children.back();
  scenesMenu.title = STR_SCENES;
  scenesMenu.parent = &menu;
  scenesMenu.currIndex = 0;

  for(auto &sceneName : sceneNames)
  {
    addActionItem(scenesMenu, sceneName.c_str(), [&sceneName](MenuItem&) {
      uint32_t sceneId = std::stoi(sceneName.substr(1));
      P64::SceneManager::load(sceneId);
    });
  }

  addRegisteredDebugVarsToMenu(menu);
  addBackToMenu(menu);
  currentMenu = &menu;
}

void Debug::Overlay::draw(P64::Scene &scene, surface_t* surf)
{
  if(!isVisible)
  {
    if(showDebugDraws)
    {
        Debug::draw(surf);
    }
    if(showFrameRate)
    {
        //Debug::printStart();
        //Debug::printf(20, 16, "%.2f\n", (double)P64::VI::SwapChain::getFPS());
        //Debug::printf(20, 16+8, "%d / %d\n", metrics->trisPostCull, metrics->trisPreCull);
        
        rdpq_sync_pipe();
        Debug::printStart();
        Debug::printf(10, 230, "FPS: %.2f", (double)P64::VI::SwapChain::getFPS());
    }
    return;
  }

  if(!metrics)metrics = (T3DMetrics*)malloc_uncached(sizeof(T3DMetrics));
  t3d_metrics_fetch(metrics); // @TODO: remove

  if(!didInit) {
    init();
    didInit = true;
  }

  if(currentMenu == nullptr) {
    buildDebugMenu();
    return; // skip drawing on the first frame to avoid handling input that toggled the menu and move the cursor unexpectedly
  }

  auto &collScene = scene.getCollision();
  uint64_t newTicksSelf = get_user_ticks();
  MEMORY_BARRIER();

  auto btn = joypad_get_buttons_pressed(JOYPAD_PORT_1);

  const size_t numItems = currentMenu->items.size();
  const size_t numChildren = currentMenu->children.size();
  const size_t totalEntries = numItems + numChildren;

  // Up / Down
  if(btn.d_up) {
    currentMenu->currIndex = (currentMenu->currIndex + totalEntries - 1) % totalEntries;
  }
  if(btn.d_down) {
    currentMenu->currIndex = (currentMenu->currIndex + 1) % totalEntries;
  }

  // LEFT / RIGHT
  if(btn.d_left || btn.d_right)
  {
    size_t idx = currentMenu->currIndex;
    if(idx == 0)
    {
        if(currentMenu->parent)
          currentMenu = currentMenu->parent;
    }
    if(idx < numChildren + 1)
    {
      if(btn.d_right)
      {
        currentMenu = &currentMenu->children[idx];
      }
    }
    else
    {
      size_t itemIdx = idx - numChildren;
      MenuItem& item = currentMenu->items[itemIdx];

      if(item.type != MenuItemType::ACTION)
      {
        std::visit([&](auto& v) {
          using T = std::decay_t<decltype(v)>;

          if constexpr (std::is_same_v<T, std::monostate>)
          {
            return;
          }
          else if constexpr (std::is_same_v<T, bool>)
          {
            v = !v;
          }
          else
          {
            if (btn.d_left)  v -= std::get<T>(item.step);
            if (btn.d_right) v += std::get<T>(item.step);
          }
          isVarDirty = true;
        }, item.value);

        if(item.onChange) item.onChange(item);
      }
      else if(item.type == MenuItemType::ACTION)
      {
        if(btn.d_right && item.onChange)
        {
          item.onChange(item);
        }
      }
    }
  }

  if(isVarDirty && Debug::Menu::getAutomaticSaveOffset() >= 0 && !eeprom_is_busy())
  {
    Debug::Menu::saveVariables(Debug::Menu::getAutomaticSaveOffset());
    isVarDirty = false;
  }


  Debug::draw(surf);

  Debug::printStart();
  if(eeprom_is_busy())
  {
    Debug::printf(250, 230, "Saving...");
  }

  collScene.debugDraw(showCollMesh, showCollBCS);

  float posX = 16;
  float posY = 130;

  if(showFrameTime) {
    rdpq_sync_pipe();

    constexpr uint32_t fbCount = 3;
    float viBarWidth = 300;

    color_t fbStateCol[6];
    fbStateCol[0] = {0x00, 0x00, 0x00, 0xFF};
    fbStateCol[1] = {0x33, 0xFF, 0x33, 0xFF};
    fbStateCol[2] = {0x22, 0x77, 0x77, 0xFF};
    fbStateCol[3] = {0x22, 0xAA, 0xAA, 0xFF};
    fbStateCol[4] = {0xAA, 0xAA, 0xAA, 0xFF};
    fbStateCol[5] = {0xAA, 0x22, 0xAA, 0xFF};

    rdpq_mode_push();
    rdpq_set_mode_fill({0,0,0, 0xFF});
    rdpq_fill_rectangle(posX, posY-2, posX + viBarWidth, posY + 7 * fbCount+1);
    rdpq_fill_rectangle(posX, posY-10, posX + viBarWidth, posY - 6);

    rdpq_mode_pop();

    posY = 64;
    if(showFrameRate)
    {    
        Debug::printf(10, 230, "FPS: %.2f", (double)P64::VI::SwapChain::getFPS());
    }

    return;
  }

  if(showFrameRate)
  {    
    Debug::printf(10, 230, "FPS: %.2f", (double)P64::VI::SwapChain::getFPS());
  }

  posY = 24;

  heap_stats_t heap_stats;
  sys_get_heap_stats(&heap_stats);

  rdpq_set_prim_color(COLOR_BVH);
  posX = Debug::printf(posX, posY, "Coll:%.2f", (double)TICKS_TO_US(collScene.ticksBVH) / 1000.0) + 4;
  rdpq_set_prim_color(COLOR_COLL);
  posX = Debug::printf(posX, posY, "%.2f", (double)TICKS_TO_US(collScene.ticks - collScene.ticksBVH) / 1000.0) + 8;
  //posX = Debug::printf(posX, posY, "Ray:%d", collScene.raycastCount) + 8;
  rdpq_set_prim_color(COLOR_ACTOR_UPDATE);
  Debug::printf(posX, posY, "%.2f", (double)TICKS_TO_US(scene.ticksActorUpdate) / 1000.0);
    rdpq_set_prim_color(COLOR_GLOBAL_UPDATE);
    posX = Debug::printf(posX, posY + 8, "%.2f", (double)TICKS_TO_US(scene.ticksGlobalUpdate) / 1000.0) + 8;
  rdpq_set_prim_color(COLOR_SCENE_DRAW);
  Debug::printf(posX, posY, "%.2f", (double)TICKS_TO_US(scene.ticksDraw - scene.ticksGlobalDraw) / 1000.0);
    rdpq_set_prim_color(COLOR_GLOBAL_DRAW);
    posX = Debug::printf(posX, posY+8, "%.2f", (double)TICKS_TO_US(scene.ticksGlobalDraw) / 1000.0)+ 8;
  rdpq_set_prim_color(COLOR_AUDIO);
  posX = Debug::printf(posX, posY, "%.2f", (double)TICKS_TO_US(P64::AudioManager::ticksUpdate) / 1000.0) + 8;

  rdpq_set_prim_color({0xFF,0xFF,0xFF, 0xFF});

  posX = surf->width - 64;
  //posX = Debug::printf(posX, posY, "A:%d/%d", scene.activeActorCount, scene.drawActorCount) + 8;
  // posX = Debug::printf(posX, posY, "T:%d", triCount) + 8;
  Debug::printf(posX-32, posY, "H:%dkb", heap_stats.used);
  Debug::printf(posX, posY+8, "O:%d\n", scene.getObjectCount());

  posX = 24;

  // === Draw current menu ===
  posY = 38;
  Debug::printf(posX, posY, "%s", currentMenu->title.c_str());
  posY += 10;

  // Child sub-menus first
  for(auto &child : currentMenu->children) {
    bool isSel = currentMenu->currIndex == (uint32_t)(&child - &currentMenu->children[0]);
    Debug::printf(posX, posY, "%c ▶ %s", isSel ? '>' : ' ', child.title.c_str());
    posY += 8;
  }

  // Items (variables) last – std::visit for printing
  for(auto &item : currentMenu->items) {
    bool isSel = currentMenu->currIndex == numChildren + (uint32_t)(&item - &currentMenu->items[0]);

    std::visit([&](const auto& v) {
      using T = std::decay_t<decltype(v)>;

      if constexpr (std::is_same_v<T, std::monostate>)
      {
        Debug::printf(posX, posY, "%c %s", isSel ? '>' : ' ', item.text.c_str());
      }
      else if constexpr (std::is_same_v<T, bool>)
      {
        Debug::printf(posX, posY, "%c %s: %c", isSel ? '>' : ' ', item.text.c_str(), v ? '1' : '0');
      }
      else if constexpr (std::is_integral_v<T>)
      {
        Debug::printf(posX, posY, "%c %s: %lld", isSel ? '>' : ' ', item.text.c_str(), (long long)v);
      }
      else if constexpr (std::is_floating_point_v<T>)
      {
        Debug::printf(posX, posY, "%c %s: %.2f", isSel ? '>' : ' ', item.text.c_str(), static_cast<double>(v));
      }
      else
      {
        Debug::printf(posX, posY, "%c %s", isSel ? '>' : ' ', item.text.c_str());
      }
    }, item.value);

    posY += 8;
  }

  // audio channels
  posX = 24;
  posY = SCREEN_HEIGHT - 24;

  posX = Debug::printf(posX, posY, "Audio ");
  {
    auto audioMetrics = P64::AudioManager::getMetrics();
    char strMask[33] = {};
    strMask[32] = '\0';
    for(uint32_t i=0; i<32; ++i) {
      bool isPlaying = audioMetrics.maskPlaying & (1 << i);
      bool isUsed    = audioMetrics.maskAlloc & (1 << i);

      if(isPlaying && isUsed)strMask[i] = '$';
      else if(isUsed)strMask[i] = '-';
      else if(isPlaying)strMask[i] = '?';
      else strMask[i] = '.';
    }
    Debug::print(posX, posY, strMask);
  }

  // Matrix slots
  if(matrixDebug)
  {
    posX = 100;
    posY = 50;

    for(uint32_t f=0; f<3; ++f) {
      Debug::printf(posX, posY, "Color[%ld]: %p\n", f, P64::VI::SwapChain::getFrameBuffer(f)->buffer);
      posY += 8;
    }

    posY = 90;
    uint32_t matCount = P64::MatrixManager::getTotalCapacity();
    for(uint32_t i=0; i<matCount; ++i) {
      bool isUsed = P64::MatrixManager::isUsed(i);
      Debug::printf(posX, posY, "%c", isUsed ? '+' : '.');
      posX += 6;
      if(i % 32 == 31) {
        posX = 100;
        posY += 8;
      }
    }
  }


  posX = 24;
  posY = 16;

  // Performance graph
  float timeCollBVH = usToWidth(TICKS_TO_US(collScene.ticksBVH));
  float timeColl = usToWidth(TICKS_TO_US(collScene.ticks - collScene.ticksBVH));
  float timeActorUpdate = usToWidth(TICKS_TO_US(scene.ticksActorUpdate));
  float timeGlobalUpdate = usToWidth(TICKS_TO_US(scene.ticksGlobalUpdate));
  float timeSceneDraw = usToWidth(TICKS_TO_US(scene.ticksDraw - scene.ticksGlobalDraw));
  float timeGlobalDraw = usToWidth(TICKS_TO_US(scene.ticksGlobalDraw));
  float timeAudio = usToWidth(TICKS_TO_US(P64::AudioManager::ticksUpdate));
  float timeSelf = usToWidth(TICKS_TO_US(ticksSelf));

  rdpq_set_mode_fill({0,0,0, 0xFF});
  rdpq_fill_rectangle(posX-1, posY-1, posX + (barWidth/2), posY + barHeight+1);
  rdpq_set_mode_fill({0x33,0x33,0x33, 0xFF});
  rdpq_fill_rectangle(posX-1 + (barWidth/2), posY-1, posX + barWidth+1, posY + barHeight+1);

  rdpq_set_fill_color(COLOR_BVH);
  rdpq_fill_rectangle(posX, posY, posX + timeCollBVH, posY + barHeight); posX += timeCollBVH;
  rdpq_set_fill_color(COLOR_COLL);
  rdpq_fill_rectangle(posX, posY, posX + timeColl, posY + barHeight); posX += timeColl;
  rdpq_set_fill_color(COLOR_ACTOR_UPDATE);
  rdpq_fill_rectangle(posX, posY, posX + timeActorUpdate, posY + barHeight); posX += timeActorUpdate;
  rdpq_set_fill_color(COLOR_GLOBAL_UPDATE);
  rdpq_fill_rectangle(posX, posY, posX + timeGlobalUpdate, posY + barHeight); posX += timeGlobalUpdate;
  rdpq_set_fill_color(COLOR_SCENE_DRAW);
  rdpq_fill_rectangle(posX, posY, posX + timeSceneDraw, posY + barHeight); posX += timeSceneDraw;
  rdpq_set_fill_color(COLOR_GLOBAL_DRAW);
  rdpq_fill_rectangle(posX, posY, posX + timeGlobalDraw, posY + barHeight); posX += timeGlobalDraw;
  rdpq_set_fill_color(COLOR_AUDIO);
  rdpq_fill_rectangle(posX, posY, posX + timeAudio, posY + barHeight); posX += timeAudio;
  rdpq_set_fill_color({0xFF,0xFF,0xFF, 0xFF});
  rdpq_fill_rectangle(24 + barWidth - timeSelf, posY, 24 + barWidth, posY + barHeight);

  newTicksSelf = get_user_ticks() - newTicksSelf;
  //if(newTicksSelf < TICKS_FROM_MS(2))
  {
    ticksSelf = newTicksSelf;
  }
}
