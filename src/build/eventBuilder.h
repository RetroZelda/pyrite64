/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, section 7). Bakes bindings into each
// object's EventLinks record, runs the project-wide checks and writes the debug name table.
#pragma once
#include <cstdint>
#include <vector>

#include "../project/events/eventBindings.h"

namespace Project { class Project; class Object; }
namespace Project::Component { struct Entry; }

namespace Build
{
  struct SceneCtx;
}

namespace Build::Events
{
  struct Record
  {
    uint32_t hash;
    uint8_t comp;
    uint8_t pin;
    uint8_t kind;
    uint8_t up;
  };

  /** One object's bindings, each with the graph (index into SceneCtx::eventContexts) that wrote it. */
  struct ObjectBindings
  {
    const Project::Object &obj;
    std::vector<std::pair<Project::Component::Entry*, size_t>> comps{}; // in the order they are written
    std::vector<std::pair<const Project::Events::Bindings*, size_t>> objectPins{};
  };

  /** Loads the catalog, global script systems and the source scan. Call after the script build. */
  void begin(Project::Project &project, SceneCtx &ctx);

  /**
   * The EventLinks records of one object. 'compList' is the object's components in the order they
   * are written; the EventLinks record goes in front of them, so component i gets index i + 1.
   * Throws on a binding that can never work (bad name, unknown system pin, wrong direction).
   */
  std::vector<Record> collect(SceneCtx &ctx, const ObjectBindings &input);

  /** Writes an EventLinks component record (id 26) into ctx.fileObj. */
  void writeLinks(SceneCtx &ctx, const Project::Object &obj, const std::vector<Record> &records);

  /** Runs the project-wide checks and writes src/p64/eventNames.cpp. False if there were errors. */
  bool finish(Project::Project &project, SceneCtx &ctx);
}
