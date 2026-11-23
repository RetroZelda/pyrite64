/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, sections 6.3 and 8.2 to 8.5). The Events
// window: the event wiring of the open scene, or of the prefab being edited, as a node graph.
// It draws what Project::Events::Graph derives from the bindings and turns wires drawn or removed
// into binding edits. Node positions live in a layout file next to the scene or prefab.
#pragma once
#include <memory>

namespace Editor
{
  class EventGraphWindow
  {
    public:
      EventGraphWindow();
      ~EventGraphWindow();

      /** Draws the window's content; call between ImGui::Begin("Events") and End(). */
      void draw();

    private:
      struct State;
      std::unique_ptr<State> state;
  };
}
