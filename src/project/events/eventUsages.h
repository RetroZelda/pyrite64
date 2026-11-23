/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, phase 7). Every place a project uses an event
// name, and renaming one everywhere: the event browser and the command line share this.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace Project { class Project; class Scene; }

namespace Project::Events::Usages
{
  struct Use
  {
    enum class Kind : uint8_t { Scene, Prefab, Canvas, Code };
    std::string name{};    // as written
    Kind kind{};
    std::string where{};   // "scene 3 (Level) / Door / TestDoor.Open", "prefab Door.prefab / ...", "src/user/x.cpp:12"
    bool input{false};     // listens (an input pin) rather than fires; code uses count as both
    int sceneId{0};        // Kind::Scene
    uint32_t object{0};    // Kind::Scene: the object, for selecting it
  };

  /**
   * Every use in the project: the scenes (the open one from memory, the others from disk), the
   * prefabs, the canvases and the "..."_event literals of the game code. Sorted by name.
   */
  std::vector<Use> collect(Project &project, Scene *openScene);

  struct RenameResult
  {
    int bindings{0};                  // bindings, overrides and links renamed
    std::vector<std::string> files{}; // scene, prefab and canvas files written
    std::vector<std::string> code{};  // code uses left as they are: "file:line"
  };

  /**
   * Renames an event in every scene, prefab and canvas (bindings, instance overrides and parent
   * links); game code is reported, not changed. The open scene is changed in memory (undo covers
   * it) and saved by the editor as usual; other files are written now.
   */
  RenameResult rename(Project &project, Scene *openScene, const std::string &from, const std::string &to);
}
