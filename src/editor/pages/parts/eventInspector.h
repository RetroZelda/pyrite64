/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, sections 6.2 and 8.1). The "Events" section the
// object inspector shows under each component: its pins and the events they are bound to.
#pragma once
#include <cstdint>
#include <vector>

namespace Project { class Object; struct Canvas; }
namespace Project::Component { struct Entry; }

namespace Editor::EventInspector
{
  /** Where the binding edits of the inspected object go. Default: the bindings themselves. */
  struct Target
  {
    // Locked prefab content: the bindings come from the prefab and are shown read-only, each
    // with a mute toggle; bindings added here belong to this instance. Both are overrides
    // stored on 'instance', for the object at 'path' below its root.
    Project::Object *instance{nullptr};
    std::vector<uint32_t> path{};

    // Prefab edit mode: pins can be exposed on the prefab's instance nodes. The list lives on
    // the edited prefab's root; 'exposePath' leads from there to the inspected object.
    Project::Object *exposeRoot{nullptr};
    std::vector<uint32_t> exposePath{};
  };

  /** Draws the Events section of a component. 'readOnly' disables every edit. */
  void drawComponent(Project::Object &obj, Project::Component::Entry &comp, bool readOnly, const Target &target = {});

  /** Draws the Events section of the object's own pins. */
  void drawObject(Project::Object &obj, bool readOnly, const Target &target = {});

  /**
   * The parent links of a prefab instance node: every numbered name its prefab reaches above
   * itself, each repointable for this instance. 'inScene': the node sits in a scene's own graph,
   * where the default target reaches nothing.
   */
  void drawLinks(Project::Object &instance, bool readOnly, bool inScene);

  /** The pins of a UI canvas and their bindings, for the canvas editor (PLAN 9). */
  void drawCanvas(Project::Canvas &canvas);
}
