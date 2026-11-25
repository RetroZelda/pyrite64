/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: event graph (docs/todo/event-graph/PLAN.md, sections 3.2, 6.4 and 7).
// Name rules, the catalog, the source scan and the project-wide checks. No UI and no project
// classes, so the host unit tests can use it directly.
#pragma once
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "../../../n64/engine/include/events/types.h"

namespace Project::Events
{
  using Scope = P64::Events::Scope;

  P64::Events::ParsedName parse(const std::string &name);

  /** Empty if 'name' is valid, otherwise one sentence saying what is wrong. */
  std::string nameProblem(const std::string &name);

  /** "scene" for "scene::Loaded"; empty if there is no "::". */
  std::string namespaceOf(const std::string &name);

  /** global, scene, ui, object, prefab and prefab_N: namespaces that need no declaration. */
  bool isBuiltinNamespace(const std::string &ns);

  /** prefab_N::X becomes prefab::X (the same event); other names are returned unchanged. */
  std::string canonicalName(const std::string &name);

  /** The record kind byte the EventLinks component reads: bit 0 input, bits 1-3 scope. */
  uint8_t recordKind(bool input, Scope scope);

  // ---- catalog: declared namespaces and descriptions (data/events/*.json, <project>/events/*.json)

  struct CatalogEvent
  {
    std::string doc{};
    std::string value{};
    bool engine{false}; // fired by the engine itself
  };

  // Fork: animation events (docs/todo/anim-events/PLAN.md). The "animationEvents" section: per
  // model asset, the output pins its Model (Animated) components get.
  struct AnimEvent
  {
    std::string name{};  // the pin name, "Leap.LAND"
    std::string doc{};
    std::string value{}; // what the value is, for the tooltip
  };

  struct AnimEventList
  {
    std::string doc{};
    std::vector<AnimEvent> events{}; // in pin order
    std::string file{};              // the catalog file that lists it, for messages
  };

  /** Empty if 'name' is a valid animation event pin name (letters, digits, _ and dots, no empty part). */
  std::string animEventNameProblem(const std::string &name);

  struct Catalog
  {
    std::map<std::string, std::string> namespaces{}; // game namespace -> description
    std::map<std::string, CatalogEvent> events{};
    std::map<std::string, AnimEventList> animationEvents{}; // model project path -> its events

    /** The animation events of a model ("assets/x.glb"), or nullptr if it lists none. */
    [[nodiscard]] const AnimEventList* findAnimEvents(const std::string &modelPath) const;

    /** Reads one file. Problems are appended to 'errors'. */
    void loadFile(const std::filesystem::path &file, bool engineFile, std::vector<std::string> &errors);
    /** Reads every *.json in 'dir', sorted by name. A missing directory is not an error. */
    void loadDir(const std::filesystem::path &dir, bool engineFiles, std::vector<std::string> &errors);

    [[nodiscard]] bool isDeclaredNamespace(const std::string &ns) const;
    [[nodiscard]] const CatalogEvent* find(const std::string &name) const;
  };

  // ---- source scan: "..."_event and "..."_pin literals in game code

  struct SourceUse
  {
    std::string name{};
    std::string file{};
    int line{0};
    bool isPin{false};
    // Fork: conditions. A name in P64_CONDITION_TRIGGERS(...): the script listens to it. The build
    // binds it to the condition's Check input, so the project index does not count it a second time.
    bool trigger{false};
  };

  std::vector<SourceUse> scanSource(const std::string &code, const std::string &fileName);
  std::vector<SourceUse> scanSourceDir(const std::filesystem::path &dir);

  /**
   * Fork: conditions. The events a node graph fires: the names of its "Fire Event" nodes (the text
   * of a .p64graph file). 'line' is 0.
   */
  std::vector<SourceUse> scanNodeGraph(const std::string &json, const std::string &fileName);

  // ---- project index, filled while a project builds

  enum class UseKind : uint8_t
  {
    BoundIn,   // an input pin of an object is bound to it (the object listens)
    BoundOut,  // an output pin of an object is bound to it (the object fires)
    Code,      // a "..."_event literal in game code (may post or listen)
    GraphOut,  // Fork: conditions, a node graph's "Fire Event" node (fires)
    SystemIn,  // an input pin of a global script system
    SystemOut, // an output pin of a global script system
    EngineOut, // fired by the engine
  };

  struct Use
  {
    std::string name{};
    UseKind kind{};
    std::string where{};
  };

  struct Message
  {
    enum class Level : uint8_t { Info, Warning, Error };
    Level level{};
    std::string text{};
  };

  struct SystemDecl
  {
    std::string system{};
    std::string script{};
    std::vector<std::string> in{};
    std::vector<std::string> out{};
  };

  struct Index
  {
    std::vector<Use> uses{};
    std::vector<SourceUse> sourcePins{};
    std::set<std::string> declaredInputPins{}; // input pin names of every object script
    std::vector<SystemDecl> systems{};
    // Fork: conditions. Names a condition's Result output is bound to, and the bindings of input
    // pins that read conditions (name, where).
    std::set<std::string> conditionResults{};
    std::vector<std::pair<std::string, std::string>> conditionReads{};

    void add(const std::string &name, UseKind kind, const std::string &where);
    /** Records a global script system and its global::System.Pin uses. Reports duplicates. */
    void addSystem(const SystemDecl &decl, std::vector<Message> &messages);
    void addSourceUses(const std::vector<SourceUse> &found);
    void addCatalog(const Catalog &catalog);

    /** The project-wide checks of PLAN section 7. */
    [[nodiscard]] std::vector<Message> check(const Catalog &catalog) const;

    /** Every distinct canonical name, sorted. */
    [[nodiscard]] std::vector<std::string> allNames() const;

    [[nodiscard]] const SystemDecl* findSystem(const std::string &system) const;
  };

  // ---- prefab scope (PLAN 3.4)

  /** One graph on the way from an object out to the scene, as the build sees it. */
  struct ScopeLevel
  {
    bool isScene{false};        // the scene's own graph: prefab:: names are not allowed here
    bool spawnedRoot{false};    // the root of a prefab built for spawning: nothing known above it
    uint8_t skip{0};            // prefab instance roots from the object up to this graph's root, the root not counted
    std::vector<std::pair<std::string, std::string>> links{}; // repointed links on this graph's instance node
  };

  struct ResolvedName
  {
    std::string problem{};    // a binding that can never work: the build fails
    std::string warning{};    // a binding that reaches nothing: it stays silent
    std::string name{};       // the name it ends up as, after following repointed links
    uint32_t hash{};
    Scope scope{Scope::Global};
    uint8_t up{};             // runtime: the (up + 1)-th prefab instance root from the object, the object counted
  };

  /**
   * Resolves a binding written in graph 'level' (levels[0] is the outermost) for one object:
   * follows numbered prefab names and repointed links outward until the graph they mean.
   */
  ResolvedName resolveName(const std::string &name, const std::vector<ScopeLevel> &levels, size_t level);

  /** prefab_N::X with N changed by 'delta' (prefab:: is N = 0); other names are returned unchanged. */
  std::string shiftPrefabNumber(const std::string &name, int delta);

  // ---- moving content across a prefab boundary in a scene (PLAN 3.4, "To Prefab" and "Unpack")

  /** 'name' is a prefab:: or prefab_N:: name. */
  bool isPrefabName(const std::string &name);

  /**
   * "To Prefab": the name that moved content writes in the new prefab's graph. A scene:: name in
   * 'local' (used by nothing outside the moved objects) becomes the prefab:: name, so copies of
   * the new prefab stay apart; every other name keeps its meaning and is returned unchanged.
   */
  std::string nameAfterWrap(const std::string &name, const std::set<std::string> &local);

  struct UnpackRules
  {
    std::vector<std::pair<std::string, std::string>> links{}; // the instance's repointed links
    std::map<std::string, std::string> local{};               // the prefab's own prefab:: names -> scene names
  };

  /**
   * "Unpack": the name that the prefab's content writes once it is part of the scene's graph.
   * Numbered names follow the instance's links one graph out. Empty when the name reaches nothing
   * in the scene (it pointed above the outermost prefab), so the binding can be dropped.
   */
  std::string nameAfterUnpack(const std::string &name, const UnpackRules &rules);

  /**
   * A scene name for a prefab:: event of an unpacked instance: scene::X, or scene::Inst.X,
   * scene::Inst_2.X, ... while 'taken' says the name is already used for something else.
   * 'prefixed' skips the plain scene::X.
   */
  std::string freeSceneName(const std::string &localName, const std::string &instanceName,
                            const std::function<bool(const std::string&)> &taken, bool prefixed = false);

  /**
   * The object output pin that stands in for a prefab life-cycle event once its instance is
   * unpacked: Ready for prefab::Spawned, Enabled, Disabled, and Destroyed for prefab::Destroying.
   * Empty for any other name.
   */
  std::string lifecyclePin(const std::string &localName);

  /**
   * Checks one binding of an object pin at build time, beyond the name rules: global:: names must
   * be a pin of a system (or the engine) facing the other way. Empty if fine.
   */
  std::string bindingProblem(const Index &index, const Catalog &catalog, const std::string &name, bool input);
}
