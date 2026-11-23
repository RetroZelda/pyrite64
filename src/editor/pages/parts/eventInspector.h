/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, section 8.1). The "Events" section the
// object inspector shows under each component: its pins and the events they are bound to.
#pragma once

namespace Project { class Object; }
namespace Project::Component { struct Entry; }
namespace Project::Events { struct Bindings; struct PinList; }

namespace Editor::EventInspector
{
  /** Draws the Events section of a component. 'readOnly' for locked prefab content. */
  void drawComponent(Project::Object &obj, Project::Component::Entry &comp, bool readOnly);

  /** Draws the Events section of the object's own pins. */
  void drawObject(Project::Object &obj, bool readOnly);
}
