/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7). A built dialog tree (.dlg) and a
// read-only view of it. The engine only holds the data; the game walks it (KC's Dialog system).
// This header must stay free of engine and libdragon includes: the editor and the host unit
// tests include it too. All numbers are big-endian.
//
//   0   char[4] "P64D"
//   4   u16 version, u16 node count
//   8   u16 start count, u16 expression count
//   12  u16 condition count, u16 input count
//   16  u32 offset of the node table, u32 offset of the expression table
//   24  u32 offset of the condition table, u32 offset of the input table
//   32  starts: u32 name hash (0 = the main start), u16 node, u16 0
//   node table: u32 offset per node; nodes: u8 type, u8 flags, u16 next, then per type
//   expression table: u32 offset per expression; bytecode ending with EXPR_END
//   condition table: u16 script index, u16 values size, u32 offset of the values
//   input table: u32 name hash per input
#pragma once
#include <cstddef>
#include <cstdint>

namespace P64::Dialog
{
  constexpr uint16_t VERSION = 1;
  constexpr uint16_t NONE = 0xFFFF;   // no next node: the dialog ends (value 0)
  constexpr int16_t NO_EXPR = -1;      // no condition: always true

  enum class NodeType : uint8_t
  {
    Start  = 0,
    Line   = 1, // u32 key, u8 speaker (0xFF none), u8 language (Lang), u8 portrait (0xFF default), u8 0
    Choice = 2, // u8 option count, u8 0, u16 0, then per option (OPTION_SIZE bytes):
                //   u32 key, u16 next, u8 option flags, u8 0, i16 show expr, i16 enable expr,
                //   u32 event hash, u8 event scope, u8 event up, u16 0, u32 event value
    Branch = 3, // i16 expr, u16 next when false (next is the true way)
    Event  = 4, // u32 event hash, u8 scope, u8 up, u16 0, u32 value
    GoTo   = 5, // u16 tree asset index (NONE: this tree), u16 0, u32 start name hash
    End    = 6, // u32 value
  };

  /** Which language a line is shown in. */
  enum class Lang : uint8_t { Speaker = 0, Game = 1, Paired = 2 };

  // Line flags
  constexpr uint8_t LINE_NO_VOICE = 1 << 0; // no voice clip, even when one exists
  constexpr uint8_t LINE_TIMED    = 1 << 1; // turns by itself after a read time, no button

  // Choice option flags
  constexpr uint8_t OPTION_CANCEL  = 1 << 0; // B picks it
  constexpr uint8_t OPTION_DEFAULT = 1 << 1; // the cursor starts on it
  constexpr uint8_t OPTION_EVENT   = 1 << 2; // fires its event when picked

  constexpr uint32_t OPTION_SIZE = 24;

  // Expression bytecode
  enum class Expr : uint8_t
  {
    End   = 0,
    Cond  = 1, // u16 condition index (big-endian): run the condition script
    Input = 2, // u8 input index: a yes/no the scene wires in (phase 7)
    And   = 3, // u8 n: the last n values
    Or    = 4,
    Xor   = 5, // an odd number of them
    Not   = 6, // u8 n: none of them (NOR); n 1 is NOT
    True  = 7, // a Branch with nothing wired into it
  };
  constexpr int EXPR_STACK = 16;

  /** A view of a built tree in memory. It never copies or owns the data. */
  struct TreeView
  {
    const uint8_t* data{};
    uint32_t size{};

    bool init(const void* fileData, uint32_t fileSize);
    bool valid() const { return data != nullptr; }

    uint16_t nodeCount() const;
    uint16_t startCount() const;
    uint16_t exprCount() const;
    uint16_t conditionCount() const;
    uint16_t inputCount() const;

    /** The node a start begins at: the main start for hash 0, NONE when there is no such start. */
    uint16_t start(uint32_t nameHash = 0) const;

    NodeType type(uint16_t node) const;
    uint8_t flags(uint16_t node) const;
    uint16_t next(uint16_t node) const;

    // Line
    uint32_t lineKey(uint16_t node) const;
    uint8_t lineSpeaker(uint16_t node) const;
    Lang lineLang(uint16_t node) const;
    uint8_t linePortrait(uint16_t node) const;

    // Choice
    uint8_t optionCount(uint16_t node) const;
    uint32_t optionKey(uint16_t node, uint8_t opt) const;
    uint16_t optionNext(uint16_t node, uint8_t opt) const;
    uint8_t optionFlags(uint16_t node, uint8_t opt) const;
    int16_t optionShow(uint16_t node, uint8_t opt) const;
    int16_t optionEnable(uint16_t node, uint8_t opt) const;
    uint32_t optionEvent(uint16_t node, uint8_t opt) const;
    uint8_t optionEventScope(uint16_t node, uint8_t opt) const;
    uint8_t optionEventUp(uint16_t node, uint8_t opt) const;
    uint32_t optionEventValue(uint16_t node, uint8_t opt) const;

    // Branch
    int16_t branchExpr(uint16_t node) const;
    uint16_t branchFalse(uint16_t node) const;

    // Event
    uint32_t eventHash(uint16_t node) const;
    uint8_t eventScope(uint16_t node) const;
    uint8_t eventUp(uint16_t node) const;
    uint32_t eventValue(uint16_t node) const;

    // GoTo
    uint16_t gotoTree(uint16_t node) const;
    uint32_t gotoStart(uint16_t node) const;

    // End
    uint32_t endValue(uint16_t node) const;

    /** The bytecode of an expression, or null. */
    const uint8_t* expr(int16_t index) const;
    uint16_t conditionScript(uint16_t cond) const;
    const void* conditionData(uint16_t cond) const;
    uint16_t conditionDataSize(uint16_t cond) const;
    uint32_t inputHash(uint8_t input) const;

    const uint8_t* nodeAt(uint16_t node) const;
  };

  /**
   * Runs an expression. 'cond' answers a condition (by index), 'input' a scene input; both get
   * 'user'. The stack is EXPR_STACK deep; a malformed expression answers false.
   */
  bool runExpr(const uint8_t* code, bool (*cond)(uint16_t, void*), bool (*input)(uint8_t, void*), void* user);
}
