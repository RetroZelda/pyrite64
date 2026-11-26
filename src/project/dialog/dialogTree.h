/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7). A dialog tree as the editor keeps it
// (a .p64dlg JSON file): nodes, their pins and the links between them, and the build of it into
// the engine's format (n64/engine/include/dialog/dialogData.h). No ImGui here, so the unit tests
// run it.
#pragma once
#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "json.hpp"

namespace Project::Dialog
{
  enum class NodeType : uint8_t { Start, Line, Choice, Branch, Condition, Logic, Input, Event, GoTo, End, Count };

  const char* typeName(NodeType t);
  NodeType typeFromName(const std::string &name);

  struct Option
  {
    std::string key{};
    bool cancel{false};
    bool isDefault{false};
    std::string event{};   // fired when picked, "" for none
    int32_t eventValue{0};
  };

  struct Node
  {
    uint32_t id{0};
    NodeType type{NodeType::Line};
    float x{0}, y{0};

    std::string name{};        // Start: the entry's name ("" is the main one); Input: its name
    std::string speaker{};     // Line: a speaker id of locale/speakers.json, "" for none
    std::string key{};         // Line: the text's key
    std::string lang{"speaker"}; // Line: speaker, game or paired
    std::string portrait{};    // Line: a portrait name of the speaker, "" for the default
    bool noVoice{false};       // Line
    bool timed{false};         // Line: turns by itself
    std::vector<Option> options{}; // Choice
    uint64_t script{0};        // Condition: the condition script's uuid
    std::map<std::string, std::string> args{}; // Condition: its values, by field name
    std::string logic{"and"};  // Logic: and, or, xor, not
    std::string event{};       // Event: the name ("shop::Bought")
    int32_t value{0};          // Event and End: the value
    uint64_t tree{0};          // GoTo: another tree's uuid, 0 for this one
    std::string start{};       // GoTo: the Start's name
  };

  /** A wire from an output pin to an input pin, by pin name. */
  struct Link
  {
    uint32_t from{0};
    std::string out{};
    uint32_t to{0};
    std::string in{};

    bool operator==(const Link&) const = default;
  };

  enum class PinKind : uint8_t { Flow, Bool };

  struct PinDef
  {
    std::string name{};
    PinKind kind{PinKind::Flow};
    bool many{false}; // an input that takes several wires (flow inputs; a logic node's Inputs)
  };

  std::vector<PinDef> inputsOf(const Node &node);
  std::vector<PinDef> outputsOf(const Node &node);

  struct Tree
  {
    std::vector<Node> nodes{};
    std::vector<Link> links{};
    uint32_t nextId{1};

    Node* find(uint32_t id);
    [[nodiscard]] const Node* find(uint32_t id) const;
    /** A new node with the next id; a Line gets a key made from 'keyPrefix' and its id. */
    Node& add(NodeType type, float x, float y, const std::string &keyPrefix);
    void remove(uint32_t id);
    /** The links into an input pin, and the one out of an output pin. */
    [[nodiscard]] std::vector<const Link*> linksInto(uint32_t node, const std::string &pin) const;
    [[nodiscard]] const Link* linkFrom(uint32_t node, const std::string &pin) const;
  };

  bool parse(const nlohmann::json &doc, Tree &out, std::vector<std::string> &errors);
  std::string serialize(const Tree &tree);

  /** The key prefix of a tree's texts: "dlg." and the file name in lower case, a-z 0-9 _. */
  std::string keyPrefixOf(const std::string &fileName);
  /** The key of a choice's option: the node's key prefix, the node id and the option index. */
  std::string optionKey(const std::string &keyPrefix, uint32_t node, size_t option);

  /** Every text key the tree uses (lines and choice options). */
  std::vector<std::string> keysOf(const Tree &tree);
  /** Every event name the tree fires. */
  std::vector<std::string> eventsOf(const Tree &tree);

  /** What the build needs from the project. */
  struct BuildEnv
  {
    /** A condition script's index in the script table, -1 when it is unknown or not a condition. */
    std::function<int(uint64_t scriptUuid)> scriptIndex{};
    /** Writes a condition script's values blob; false when it cannot (the error is in 'error'). */
    std::function<bool(uint64_t scriptUuid, const std::map<std::string, std::string> &args, std::vector<uint8_t> &out, std::string &error)> writeArgs{};
    /** A speaker's index, -1 for an unknown id. */
    std::function<int(const std::string &id)> speakerIndex{};
    /** A portrait's index within a speaker, -1 for an unknown name. */
    std::function<int(const std::string &speaker, const std::string &portrait)> portraitIndex{};
    /** Another tree's asset index, -1 when unknown. */
    std::function<int(uint64_t uuid)> treeAssetIndex{};
  };

  /**
   * Builds the engine's format. Problems that make the tree unusable go to 'errors'; a node no
   * flow reaches is only reported in 'warnings'.
   */
  std::vector<uint8_t> build(const Tree &tree, const BuildEnv &env, std::vector<std::string> &errors, std::vector<std::string> &warnings);
}
