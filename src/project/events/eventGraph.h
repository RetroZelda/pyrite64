/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, sections 8.2 and 8.3). What the Events window
// shows, derived from the bindings, and the edits a wire makes to them. No UI here: the window
// and the command line use the same functions.
//
// A graph is the bindings written in one file: a scene, the prefab being edited, or (focus mode)
// one object. Each object is a node; a prefab instance is one node with its exposed pins and
// parent links. Wires are not stored: an output and an input bound to the same event are wired.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace Project { class Project; class Object; }

namespace Project::Events::Graph
{
  /** The file a graph shows. In focus mode it shows one object of it and creates object:: events. */
  enum class Kind : uint8_t
  {
    Scene,  // a scene's own graph: creates scene:: events
    Prefab, // the file of the prefab being edited: creates prefab:: events
  };

  /** Where a pin lives. */
  struct PinRef
  {
    uint32_t node{};              // the object the node stands for
    std::vector<uint32_t> path{}; // instance nodes: the inner object, from the instance root down; empty: the node itself
    uint64_t comp{0};             // component uuid, 0: the object's own pins
    std::string pin{};            // the pin; for a parent link, the link as the prefab writes it
    bool input{false};
    bool link{false};             // a parent link of an instance node
    uint64_t canvas{0};           // a pin of this UI canvas (node is 0), PLAN 9

    [[nodiscard]] bool valid() const { return node != 0 || canvas != 0; }
    bool operator==(const PinRef&) const = default;
    bool operator<(const PinRef &other) const;
  };

  struct Pin
  {
    PinRef ref{};
    std::string label{};
    std::vector<std::string> names{}; // the events it is bound to, as this graph writes them
    std::vector<std::string> muted{}; // instance pins: the prefab's bindings muted on this instance
    bool declared{true};              // false: the script lost the pin, its bindings do nothing
    std::string problem{};            // why a binding cannot work (drawn red)
    std::string hint{};               // a binding that reaches nothing here
    // Fork: conditions (docs/todo/conditionals/PLAN.md)
    std::vector<std::string> locked{}; // events the script itself binds to the pin (P64_CONDITION_TRIGGERS
                                       // on Check): wired like 'names', but not stored in the file
    bool result{false};               // the Result output of a condition
    bool reads{false};                // an input that reads conditions: Result outputs are wired into it
  };

  /** Pins of a node that belong together: the object's own, a component's, an inner object's, the links. */
  struct Category
  {
    std::string key{};   // stable id for the layout: "o", "c:<comp>", "p:<path>:<comp>", "links"
    std::string title{};
    bool openByDefault{true};
    std::vector<Pin> pins{};
  };

  struct Node
  {
    uint32_t uuid{};
    std::string name{};
    bool instance{false};
    std::string prefab{};  // instance nodes: the prefab's name
    bool builtin{false};   // a built-in event object (an engine script)
    bool delay{false};     // Delay or Frame Delay: a loop through it is fine
    bool condition{false}; // Fork: conditions, has a condition script
    std::string summary{}; // Fork: conditions, its static values ("Compare: Less, Value: 25")
    uint64_t canvas{0};    // a UI canvas node (uuid is 0)
    std::vector<Category> categories{};

    [[nodiscard]] bool bound() const;
  };

  /** A global script's system on the Global node; "Engine" holds the events the engine fires. */
  struct System
  {
    std::string name{};
    std::vector<std::string> in{};  // objects fire into these (inputs of the Global node)
    std::vector<std::string> out{}; // objects listen to these (outputs of the Global node)
  };

  struct Model
  {
    Kind kind{};
    uint32_t focus{0};     // focus mode: the one object shown
    std::string ns{};      // namespace of the events the graph creates
    std::string title{};
    std::vector<Node> nodes{};              // in tree order
    std::vector<Node> canvases{};           // every UI canvas of the project, PLAN 9
    std::vector<System> systems{};          // sorted, Engine last
    std::vector<std::string> scopeEvents{}; // the scope node: scene::Loaded, ... or prefab::Spawned, ...
    std::set<std::string> codeNames{};      // "..."_event literals in game code
    std::set<std::string> allNames{};       // every name some pin is bound to

    [[nodiscard]] const Node* find(uint32_t uuid) const;
    [[nodiscard]] const Node* findCanvas(uint64_t uuid) const;
    [[nodiscard]] const Pin* findPin(const PinRef &ref) const;
  };

  /**
   * The graph of a scene (root: the scene's root object, which is not a node itself) or of the
   * prefab being edited (root: its definition's root, which is a node). 'focus': only that object.
   */
  Model collect(Project &project, Object &root, Kind kind, uint32_t focus = 0);

  /** One end of a wire. */
  struct End
  {
    enum class Type : uint8_t { Pin, Event, Global, Scope };
    Type type{Type::Pin};
    PinRef pin{}; // for Pin
    bool operator==(const End&) const = default;
  };

  struct Wire
  {
    End from{};       // an output pin, or the output of an Event/Global/Scope node
    End to{};         // an input pin, or the input of an Event/Global node
    std::string name; // the event
  };

  /**
   * The wires: an output and an input bound to the same event. Names in 'eventNodes' are routed
   * through their event node; global:: names go to the Global node and scope events come from the
   * scope node when those are shown. object:: names only wire pins of the same object.
   */
  std::vector<Wire> wires(const Model &model, const std::set<std::string> &eventNodes, bool globalShown, bool scopeShown);

  /** Normalises a name for comparisons: prefab_0:: is prefab::. */
  std::string key(const std::string &name);

  /** The event an output pin uses on a new wire: its first one that connects here, or empty. */
  std::string wireName(const Model &model, const PinRef &out);

  /**
   * The name a new wire from 'out' creates: <ns>::<Object>.<Pin>, made unique in the graph. A wire
   * to or from a canvas makes a ui:: event, since a canvas belongs to no graph (pass ns "ui").
   */
  std::string autoName(const Model &model, const PinRef &out, const std::string &ns = {});

  // Edits. 'root' is the same object collect() was given; each returns true if something changed.

  /** Binds a pin to an event. On an instance: an override of that instance, or a repointed link. */
  bool bind(Project &project, Object &root, const Model &model, const PinRef &pin, const std::string &name);

  /** Removes a binding. On an instance: drops the override, or mutes the prefab's own binding. */
  bool unbind(Project &project, Object &root, const Model &model, const PinRef &pin, const std::string &name);

  /** A wire drawn from an output to an input: both end up bound to one event. Returns it. */
  std::string connect(Project &project, Object &root, const Model &model, const PinRef &out, const PinRef &in);

  /**
   * Removes a wire: the input stops listening. The output stops firing too when nothing else in
   * the graph or the game code uses the event and it is one of the graph's own (<ns>::).
   */
  bool disconnect(Project &project, Object &root, const Model &model, const Wire &wire);

  /**
   * A pin as text, for the command line and messages: the object's path, '|', then the category
   * and the pin. "Lever|LeverScript.Pulled", "Lever|Object.Enable", "K1/Plate|TestPlate.Press"
   * (a pin inside the instance K1), "K2|link.prefab_1::Alarm" (a parent link), "canvas:HUD|Show".
   */
  std::string describe(Project &project, Object &root, const PinRef &ref);

  /** The reverse of describe(). The node is 0 when nothing matches. */
  PinRef parsePin(Project &project, Object &root, const Model &model, const std::string &text, bool input);
}
