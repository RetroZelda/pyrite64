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

  struct Catalog
  {
    std::map<std::string, std::string> namespaces{}; // game namespace -> description
    std::map<std::string, CatalogEvent> events{};

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
  };

  std::vector<SourceUse> scanSource(const std::string &code, const std::string &fileName);
  std::vector<SourceUse> scanSourceDir(const std::filesystem::path &dir);

  // ---- project index, filled while a project builds

  enum class UseKind : uint8_t
  {
    BoundIn,   // an input pin of an object is bound to it (the object listens)
    BoundOut,  // an output pin of an object is bound to it (the object fires)
    Code,      // a "..."_event literal in game code (may post or listen)
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

  /**
   * Checks one binding of an object pin at build time, beyond the name rules: global:: names must
   * be a pin of a system (or the engine) facing the other way. Empty if fine.
   */
  std::string bindingProblem(const Index &index, const Catalog &catalog, const std::string &name, bool input);
}
