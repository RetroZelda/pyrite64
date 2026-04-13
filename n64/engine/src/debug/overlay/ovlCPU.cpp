/**
* @copyright 2024 - Max Bebök
* @license MIT
*/
#include "../overlay.h"
#include "audio/audioManager.h"
#include "debug/debugDraw.h"
#include "lib/matrixManager.h"
#include "scene/scene.h"
#include "scene/sceneManager.h"
#include "vi/swapChain.h"

#include <array>
#include <vector>
#include <algorithm>
#include <string>

namespace
{
  #include "ovlColors.h"

  template<typename T, uint32_t N>
  struct SlidingWindowAvg
  {
    void push(T v) {
      values[idx] = v;
      idx = (idx + 1) % N;
      if (count < N) ++count;
    }
    T avg() const {
      if (count == 0) return 0;
      uint64_t sum = 0;
      for (size_t i = 0; i < count; ++i) sum += values[i];
      return sum / count;
    }
    T latest() const {
      return values[(idx + N - 1) % N];
    }

    std::array<T, N> values{};
    uint16_t idx{};
    uint16_t count{};
  };

  struct TimeEntry {
    const char *label{};
    uint64_t ticks{};
    color_t col{0xFF, 0xFF, 0xFF, 0};
    uint64_t count{0};
  };

  void printTable(const char* title, uint16_t posX, uint16_t posY, uint16_t txtWidth, const TimeEntry* entries, uint32_t numEntries, bool splitNameAndTime = false)
  {
    uint64_t timeSum = 0;
    for(size_t i = 0; i < numEntries; ++i) {
      const auto &entry = entries[i];
      timeSum += entry.ticks;
    }
    timeSum = TICKS_TO_US(timeSum);

    uint16_t posXStart = posX;

    P64::Debug::setColor({0xBB, 0xBB, 0xFF, 0xFF});
    P64::Debug::print(posX, posY, title);
    P64::Debug::setColor();
    posY += 9;

    char buf[64];
    for(size_t i = 0; i < numEntries; ++i)
    {
      const auto &entry = entries[i];
      auto us = TICKS_TO_US(entry.ticks);
      uint64_t perc = timeSum > 0 ? (us * 100) / timeSum : 0;

      // === Name + Count line ===
      posX = posXStart;
      if(entry.col.a) P64::Debug::setColor(entry.col);

      if (entry.count > 0)
        std::snprintf(buf, sizeof(buf), "%s (x%llu)", entry.label, entry.count);
      else
        std::snprintf(buf, sizeof(buf), "%s", entry.label);

      P64::Debug::print(posX, posY, buf);

      if(entry.col.a) P64::Debug::setColor();

      // === Timing line (same X position as before) ===
      if (splitNameAndTime)
      {
        posY += 8;                     // move to next line
        posX = posXStart + txtWidth;   // same column as before

        auto numTxt = std::to_string(us);
        posX += (5 - (numTxt.size())) * 8;

        posX = P64::Debug::print(posX, posY, numTxt.c_str());
        P64::Debug::print(posX, posY, DEBUG_CHAR_US);

        posX += 10;
        if(perc < 10) posX += 8;

        auto percTxt = std::to_string(perc);
        posX = P64::Debug::print(posX, posY, percTxt.c_str());
        P64::Debug::print(posX, posY, "%");
        posY += 2;
      }
      else
      {
        // old single-line behavior
        posX = posXStart + txtWidth;

        auto numTxt = std::to_string(us);
        posX += (5 - (numTxt.size())) * 8;

        posX = P64::Debug::print(posX, posY, numTxt.c_str());
        P64::Debug::print(posX, posY, DEBUG_CHAR_US);

        posX += 10;
        if(perc < 10) posX += 8;

        auto percTxt = std::to_string(perc);
        posX = P64::Debug::print(posX, posY, percTxt.c_str());
        P64::Debug::print(posX, posY, "%");
      }

      posY += 9;
    }

    // Total line
    posX = posXStart;
    auto numTxt = std::to_string(timeSum);
    P64::Debug::setColor({0xBB, 0xBB, 0xFF, 0xFF});
    posX = P64::Debug::print(posX, posY, DEBUG_CHAR_SQUARE " Total: ");
    posX = P64::Debug::print(posX, posY, numTxt.c_str());
    P64::Debug::print(posX, posY, DEBUG_CHAR_US);
    P64::Debug::setColor();
  }

  constexpr uint32_t AVG_FRAMES = 32;

  constinit SlidingWindowAvg<uint32_t, AVG_FRAMES> sw_collDet, sw_collRes, sw_updObj, sw_updMisc, sw_drawObj, sw_drawMisc, sw_audio, sw_debug;
  constinit SlidingWindowAvg<uint32_t, AVG_FRAMES> sw_wake, sw_world, sw_intVel, sw_detBody, sw_detMesh, sw_refresh, sw_preSolve, sw_warmStart, sw_velSolve, sw_integrate, sw_posSolve, sw_finalize;

  static std::unordered_map<uint8_t, SlidingWindowAvg<uint32_t, AVG_FRAMES>> sw_component_update_map;
  static std::unordered_map<uint8_t, SlidingWindowAvg<uint32_t, AVG_FRAMES>> sw_component_draw_map;
  static std::unordered_map<uint8_t, SlidingWindowAvg<uint32_t, AVG_FRAMES>> sw_script_update_map;
  static std::unordered_map<uint8_t, SlidingWindowAvg<uint32_t, AVG_FRAMES>> sw_script_draw_map;
  static std::unordered_map<std::string, SlidingWindowAvg<uint32_t, AVG_FRAMES>> sw_user_update_map;
  static std::unordered_map<std::string, SlidingWindowAvg<uint32_t, AVG_FRAMES>> sw_user_draw_map;
}

void P64::Debug::Overlay::ovlCPU()
{
  auto &scene = SceneManager::getCurrent();
  auto &collScene = scene.getCollision();

  setColor();
  uint16_t posY = 54;

  // Push new values each frame
  sw_collDet.push(collScene.ticksTotal);
  sw_collRes.push(collScene.ticksTotal - collScene.ticksDetect);
  sw_updObj.push(scene.ticksActorUpdate);
  sw_updMisc.push(scene.ticksGlobalUpdate);
  sw_drawObj.push(scene.ticksDraw - scene.ticksGlobalDraw);
  sw_drawMisc.push(scene.ticksGlobalDraw);
  sw_audio.push(P64::AudioManager::ticksUpdate);
  sw_debug.push(ticksSelf);

  sw_wake.push(collScene.ticksWakePrep);
  sw_world.push(collScene.ticksWorldUpdate);
  sw_intVel.push(collScene.ticksIntegrateVel);
  sw_detBody.push(collScene.ticksDetectBodyPairs);
  sw_detMesh.push(collScene.ticksDetectMeshPairs);
  sw_refresh.push(collScene.ticksRefreshCallbacks);
  sw_preSolve.push(collScene.ticksPreSolve);
  sw_warmStart.push(collScene.ticksWarmStart);
  sw_velSolve.push(collScene.ticksVelocitySolve);
  sw_integrate.push(collScene.ticksIntegration);
  sw_posSolve.push(collScene.ticksPositionSolve);
  sw_finalize.push(collScene.ticksFinalize);

  // Use average or latest depending on doAvg
  auto get = [](auto &sw) -> uint64_t { return useCpuAvg ? sw.avg() : sw.latest(); };

  const TimeEntry generalTiming[] = {
    {"Coll Det.", get(sw_collDet), COLOR_COLL_DETECT},
    {"Coll Res.", get(sw_collRes), COLOR_COLL},
    {"Upd. Obj", get(sw_updObj), COLOR_ACTOR_UPDATE},
    {"Upd. Misc", get(sw_updMisc), COLOR_GLOBAL_UPDATE},
    {"Draw Obj", get(sw_drawObj), COLOR_SCENE_DRAW},
    {"Draw Misc", get(sw_drawMisc), COLOR_GLOBAL_DRAW},
    {"Audio", get(sw_audio), COLOR_AUDIO},
    {"Debug", get(sw_debug)},
  };
  printTable(DEBUG_CHAR_SQUARE " General ", 16, posY, 66, generalTiming, sizeof(generalTiming) / sizeof(TimeEntry));

  const TimeEntry collTimingEntries[] = {
    {"Wake", get(sw_wake)},
    {"World", get(sw_world)},
    {"Int.Vel", get(sw_intVel)},
    {"Det.Body", get(sw_detBody)},
    {"Det.Mesh", get(sw_detMesh)},
    {"Refresh", get(sw_refresh)},
    {"PreSolve", get(sw_preSolve)},
    {"WarmStart", get(sw_warmStart)},
    {"Vel.Solve", get(sw_velSolve)},
    {"Integrate", get(sw_integrate)},
    {"Pos.Solve", get(sw_posSolve)},
    {"Finalize", get(sw_finalize)},
  };

  printTable(DEBUG_CHAR_SQUARE " Collision ", 164, posY, 66, collTimingEntries, sizeof(collTimingEntries) / sizeof(TimeEntry));
}

void P64::Debug::Overlay::ovlComponents()
{
  auto &scene = SceneManager::getCurrent();

  setColor();
  uint16_t posY = 54;

  // Use average or latest depending on doAvg
  auto get = [](auto &sw) -> uint64_t { return useCpuAvg ? sw.avg() : sw.latest(); };

  // show per-object average or totals for all
  auto get_update = [](auto& compTick) -> uint64_t { return useDetailedTotals ? compTick.update : compTick.update / compTick.count;};
  auto get_draw = [](auto& compTick) -> uint64_t { return useDetailedTotals ? compTick.draw : compTick.draw / compTick.count;};

  // Push new values each frame (only if the component actually ran)
  for (const auto& [compId, compTick] : scene.ticksComponents)
  {
    if (compTick.count > 0)
    {
      sw_component_update_map[compId].push(get_update(compTick));
      sw_component_draw_map[compId].push(get_draw(compTick));
    }
  }

  auto getComponentLabel = [](uint8_t id) -> const char* {
    switch (id)
    {
      case 0:  return "Code";
      case 1:  return "Model";
      case 2:  return "Light";
      case 3:  return "Camera";
      case 4:  return "CollMesh";
      case 5:  return "CollBody";
      case 6:  return "Audio2D";
      case 7:  return "Constraint";
      case 8:  return "Culling";
      case 9:  return "NodeGraph";
      case 10: return "AnimModel";
      case 11: return "RigidBody";
      
      default: break;// return "Comp X";
    }

    static std::unordered_map<uint8_t, std::string> nameCache;
    auto it = nameCache.find(id);
    if (it == nameCache.end())
    {
      nameCache[id] = "Comp " + std::to_string(id);   // placeholder
      it = nameCache.find(id);
    }
    return it->second.c_str();
  };

  {
    std::vector<TimeEntry> entries;
    entries.reserve(sw_component_update_map.size());

    for (const auto& [id, sw] : sw_component_update_map)
    {
      entries.push_back({getComponentLabel(id), get(sw), {}, scene.ticksComponents[id].count});
    }

    // Sort by time descending (most expensive components first - very useful for profiling)
    std::sort(entries.begin(), entries.end(),
      [](const TimeEntry& a, const TimeEntry& b) { return a.ticks > b.ticks; });

    if (!entries.empty())
      printTable(DEBUG_CHAR_SQUARE " Comp Update ", 16, posY, 66, entries.data(), (uint32_t)entries.size(), true);
  }

  {
    std::vector<TimeEntry> entries;
    entries.reserve(sw_component_draw_map.size());

    for (const auto& [id, sw] : sw_component_draw_map)
    {
      entries.push_back({getComponentLabel(id), get(sw), {}, scene.ticksComponents[id].count});
    }

    std::sort(entries.begin(), entries.end(),
      [](const TimeEntry& a, const TimeEntry& b) { return a.ticks > b.ticks; });

    if (!entries.empty())
      printTable(DEBUG_CHAR_SQUARE " Comp Draw  ", 164, posY, 66, entries.data(), (uint32_t)entries.size(), true);
  }
}

namespace P64::Script {
	extern const char* const scriptNames[];
    extern const size_t numNames;
};
void P64::Debug::Overlay::ovlScripts()
{
  auto &scene = SceneManager::getCurrent();

  setColor();
  uint16_t posY = 54;

  // Use average or latest depending on doAvg
  auto get = [](auto &sw) -> uint64_t { return useCpuAvg ? sw.avg() : sw.latest(); };

  // show per-object average or totals for all
  auto get_update = [](auto& compTick) -> uint64_t { return useDetailedTotals ? compTick.update : compTick.update / compTick.count;};
  auto get_draw = [](auto& compTick) -> uint64_t { return useDetailedTotals ? compTick.draw : compTick.draw / compTick.count;};

  // Push new values each frame (only if the script actually ran)
  for (const auto& [scriptId, tickData] : scene.ticksScripts)
  {
    if (tickData.count > 0)
    {
      sw_script_update_map[scriptId].push(get_update(tickData));
      sw_script_draw_map[scriptId].push(get_draw(tickData));
    }
  }

  // Script name lookup (add real names to the switch as you discover IDs)
  auto getScriptLabel = [](uint8_t id) -> const char* {
    
    if(id < P64::Script::numNames) return P64::Script::scriptNames[id];

    // fallback to cached "Script X"
    static std::unordered_map<uint8_t, std::string> nameCache;
    auto it = nameCache.find(id);
    if (it == nameCache.end())
    {
      nameCache[id] = "Script " + std::to_string(id);
      it = nameCache.find(id);
    }
    return it->second.c_str();
  };

  // === Script Update Table (left side) ===
  {
    std::vector<TimeEntry> entries;
    entries.reserve(sw_script_update_map.size());

    for (const auto& [id, sw] : sw_script_update_map)
    {
      entries.push_back({getScriptLabel(id), get(sw), {}, scene.ticksScripts[id].count});
    }

    std::sort(entries.begin(), entries.end(),
      [](const TimeEntry& a, const TimeEntry& b) { return a.ticks > b.ticks; });

    if (!entries.empty())
      printTable(DEBUG_CHAR_SQUARE " Script Update ", 16, posY, 66, entries.data(), (uint32_t)entries.size(), true);
  }

  // === Script Draw Table (right side) ===
  {
    std::vector<TimeEntry> entries;
    entries.reserve(sw_script_draw_map.size());

    for (const auto& [id, sw] : sw_script_draw_map)
    {
      entries.push_back({getScriptLabel(id), get(sw), {}, scene.ticksScripts[id].count});
    }

    std::sort(entries.begin(), entries.end(),
      [](const TimeEntry& a, const TimeEntry& b) { return a.ticks > b.ticks; });

    if (!entries.empty())
      printTable(DEBUG_CHAR_SQUARE " Script Draw  ", 164, posY, 66, entries.data(), (uint32_t)entries.size(), true);
  }
}

void P64::Debug::Overlay::ovlUserGlobals()
{
  auto &scene = SceneManager::getCurrent();

  setColor();
  uint16_t posY = 54;

  // Use average or latest depending on doAvg
  auto get = [](auto &sw) -> uint64_t { return useCpuAvg ? sw.avg() : sw.latest(); };

  // show per-object average or totals for all
  auto get_update = [](auto& compTick) -> uint64_t { return useDetailedTotals ? compTick.update : compTick.update / compTick.count;};
  auto get_draw   = [](auto& compTick) -> uint64_t { return useDetailedTotals ? compTick.draw   : compTick.draw   / compTick.count;};

  for (const auto& [name, tickData] : scene.ticksUserGlobals)
  {
    if (tickData.count > 0)
    {
      sw_user_update_map[name].push(get_update(tickData));
      sw_user_draw_map[name].push(get_draw(tickData));
    }
  }

  // === User Globals Update Table (left side) ===
  {
    std::vector<TimeEntry> entries;
    entries.reserve(sw_user_update_map.size());

    for (const auto& [name, sw] : sw_user_update_map)
    {
      entries.push_back({name.c_str(), get(sw), {}, scene.ticksUserGlobals[name].count});
    }

    // Sort by time descending (most expensive first)
    std::sort(entries.begin(), entries.end(),
      [](const TimeEntry& a, const TimeEntry& b) { return a.ticks > b.ticks; });

    if (!entries.empty())
      printTable(DEBUG_CHAR_SQUARE " User Update ", 16, posY, 66, entries.data(), (uint32_t)entries.size(), true);
  }

  // === User Globals Draw Table (right side) ===
  {
    std::vector<TimeEntry> entries;
    entries.reserve(sw_user_draw_map.size());

    for (const auto& [name, sw] : sw_user_draw_map)
    {
      entries.push_back({name.c_str(), get(sw), {}, scene.ticksUserGlobals[name].count});
    }

    std::sort(entries.begin(), entries.end(),
      [](const TimeEntry& a, const TimeEntry& b) { return a.ticks > b.ticks; });

    if (!entries.empty())
      printTable(DEBUG_CHAR_SQUARE " User Draw  ", 164, posY, 66, entries.data(), (uint32_t)entries.size(), true);
  }
}