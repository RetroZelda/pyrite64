/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7). What the engine knows about dialog:
// the trees' data (dialogData.h), the project's speakers, and how to answer a tree's conditions.
// Walking a tree and showing it is the game's (Kentucky Castle's Dialog system).
#pragma once
#include <cstdint>
#include "dialog/dialogData.h"

namespace P64 { class Object; }

namespace P64::Dialog
{
  /** The type of a script field that points at a tree: P64::AssetRef<P64::Dialog::dialog_t> tree; */
  struct dialog_t;

  /** For TreeView::init when the data comes from the asset manager, whose size is not kept. */
  constexpr uint32_t SIZE_UNKNOWN = 0x7FFFFFFF;

  constexpr uint16_t NO_ASSET = 0xFFFF;
  constexpr uint8_t NO_SPEAKER = 0xFF;

  struct Portrait
  {
    const char* name;  // "normal", "angry"
    uint16_t asset;    // image asset index
  };

  /** A speaker of locale/speakers.json (generated, src/p64/dialogTable.cpp). */
  struct Speaker
  {
    const char* id;        // "tank"
    uint32_t name;         // id of the name's key ("speaker.tank")
    Lang language;         // Game or Paired: the language of the speaker's lines by default
    uint8_t fontSet;       // 0xFF: the language's own
    uint8_t blipEvery;     // a blip every n letters, 0 for none
    uint8_t portraitCount;
    uint32_t nameColor;    // RGBA of the name plate
    uint16_t blipSound;    // audio asset index, NO_ASSET for none
    uint16_t reserved;
    float blipPitch;       // random pitch spread, 0.1 = +-10%
    const char* talkClip;  // animation the speaker's model plays while its text appears, "" for none
    const Portrait* portraits;
  };

  namespace Generated
  {
    /** Defined by the generated src/p64/dialogTable.cpp, called once at boot. */
    void registerSpeakers();
  }

  void setSpeakers(const Speaker* list, uint8_t count);
  uint8_t speakerCount();
  const Speaker* speaker(uint8_t index);
  /** The index of a speaker id ("tank"), or NO_SPEAKER. */
  uint8_t findSpeaker(const char* id);
  /** A speaker's portrait by name, the first one for null or an unknown name; NO_ASSET without any. */
  uint16_t portraitAsset(uint8_t speakerIndex, uint8_t portrait);

  /**
   * Answers an expression of a tree (a Branch, or a choice option's Show if / Enabled if).
   * Its Condition nodes run their condition scripts on 'obj', the object that started the dialog.
   * 'input' answers the tree's Input nodes; without it they are false.
   */
  bool evaluate(const TreeView &tree, int16_t expr, Object &obj, bool (*input)(uint8_t, void*) = nullptr, void* user = nullptr);
}
