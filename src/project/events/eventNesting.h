/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, section 3.4, "Keeping the scope when nesting
// changes"). "To Prefab" and "Unpack" move content between a scene's graph and a prefab's graph;
// these fix-ups rewrite its event names so every binding keeps reaching what it reached before.
#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace Project { class Project; class Object; }

namespace Project::Events::Nesting
{
  /** One sentence per renamed, repointed or dropped binding, for the log. */
  using Report = std::vector<std::string>;

  /**
   * "To Prefab" on a plain scene object, before its subtree is written into the new prefab:
   * scene:: events that nothing outside the subtree uses (no other object, prefab, game code or
   * engine event) become the new prefab's own prefab:: events, so copies of it stay apart.
   * Every other name keeps its meaning and is left alone.
   */
  void beforeWrap(Project &project, Object &sceneRoot, Object &obj, Report &report);

  /** One node of an unpacked instance: the definition node and the scene object made from it. */
  struct UnpackedNode
  {
    const Object *def{};           // node of the prefab's definition (its root for the instance itself)
    Object *obj{};                 // the scene object made from it (the instance node for the root)
    std::vector<uint32_t> path{};  // definition uuids from the instance root down to 'def', as overrides store it
  };

  /**
   * "Unpack", once the instance's content exists as scene objects: gives them the definition's
   * bindings in the scene's terms, applies the instance's overrides (and clears them), moves the
   * overrides meant for nested instances onto those and repoints their parent links.
   * The prefab's own prefab:: events become scene events, renamed with the instance's name when
   * the scene already uses the name; its life-cycle events (prefab::Spawned, ...) are fired by
   * the root object's own pins (Ready, ...) instead. A binding that reached nothing before (a
   * numbered name above the outermost prefab) is dropped and reported.
   */
  void afterUnpack(Project &project, Object &sceneRoot, Object &inst,
                   const std::vector<UnpackedNode> &nodes, Report &report);

  /** Numbered names (prefab_1:: and up) a prefab's content reaches above its own graph, as the prefab writes them. Sorted. */
  std::vector<std::string> parentLinks(Project &project, uint64_t prefabUUID);

  /** How a prefab's content uses one of its parent links. */
  struct LinkUse
  {
    bool listened{false}; // an input inside the prefab is bound to it
    bool fired{false};    // an output inside the prefab is bound to it
  };

  /** parentLinks() with how each link is used. */
  std::map<std::string, LinkUse> parentLinkUses(Project &project, uint64_t prefabUUID);
}
