/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4.8).
#include "text/loc.h"
#include "text/locTable.h"

#include <libdragon.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lib/logger.h"

namespace
{
  struct Loaded
  {
    uint8_t lang{P64::Loc::NO_LANGUAGE};
    void* file{};
    P64::Loc::TableView table{};
  };

  const P64::Loc::Language* languages{};
  uint8_t langCount{};
  Loaded slots[2]{};
  uint32_t langEpoch{};
  bool markFallbacks{};
  bool markMissing{};

  // "#1234ABCD" for unknown ids when markMissing is on; a few at once stay valid
  char missingText[4][12]{};
  uint8_t missingNext{};

  bool loadSlot(Loaded &out, uint8_t lang)
  {
    int size = 0;
    void* file = asset_load(languages[lang].file, &size);
    if(!file) return false;
    P64::Loc::TableView view{};
    if(!view.init(file, static_cast<uint32_t>(size))) {
      P64::Log::error("Loc: %s is not a string table of this version", languages[lang].file);
      free(file);
      return false;
    }
    out = {lang, file, view};
    return true;
  }

  void freeSlot(Loaded &slot, const Loaded &other)
  {
    if(slot.file && slot.file != other.file) free(slot.file);
    slot = {};
  }

  const Loaded& slotOf(P64::Loc::Slot slot)
  {
    return slots[slot == P64::Loc::Slot::Paired ? 1 : 0];
  }
}

void P64::Loc::setLanguageList(const Language* list, uint8_t count)
{
  languages = list;
  langCount = count;
}

uint8_t P64::Loc::languageCount() { return langCount; }

const P64::Loc::Language* P64::Loc::language(uint8_t index)
{
  return index < langCount ? &languages[index] : nullptr;
}

uint8_t P64::Loc::findLanguage(const char* code)
{
  for(uint8_t i = 0; i < langCount; ++i) {
    if(strcmp(languages[i].code, code) == 0) return i;
  }
  return NO_LANGUAGE;
}

bool P64::Loc::setLanguage(uint8_t index)
{
  if(index >= langCount) return false;

  const uint8_t pairIdx = languages[index].pair < langCount ? languages[index].pair : index;

  Loaded game{};
  if(!loadSlot(game, index)) {
    Log::error("Loc: cannot load the table of '%s'", languages[index].code);
    return false;
  }
  Loaded pair = game;
  if(pairIdx != index && !loadSlot(pair, pairIdx)) {
    Log::warn("Loc: cannot load the paired language '%s', using '%s' for it", languages[pairIdx].code, languages[index].code);
    pair = game;
  }

  // free the old tables (the paired slot may share the game slot's file)
  Loaded oldGame = slots[0];
  Loaded oldPair = slots[1];
  freeSlot(oldPair, oldGame);
  freeSlot(oldGame, Loaded{});

  slots[0] = game;
  slots[1] = pair;
  ++langEpoch;
  return true;
}

uint8_t P64::Loc::current() { return slots[0].lang; }
uint8_t P64::Loc::paired() { return slots[1].lang; }
uint8_t P64::Loc::languageOf(Slot slot) { return slotOf(slot).lang; }
uint32_t P64::Loc::epoch() { return langEpoch; }

const char* P64::Loc::text(LocId id, Slot slot)
{
  const auto &loaded = slotOf(slot);
  const int32_t entry = loaded.table.find(id.hash);
  if(entry >= 0) return loaded.table.textAt(entry);
  if(!markMissing || id.hash == 0) return "";

  char* buf = missingText[missingNext];
  missingNext = (missingNext + 1) & 3;
  snprintf(buf, sizeof(missingText[0]), "#%08lX", static_cast<unsigned long>(id.hash));
  Log::warn("Loc: no text for id %s in '%s'", buf, loaded.lang < langCount ? languages[loaded.lang].code : "-");
  return buf;
}

bool P64::Loc::has(LocId id, Slot slot)
{
  return slotOf(slot).table.find(id.hash) >= 0;
}

bool P64::Loc::isFallback(LocId id, Slot slot)
{
  const auto &loaded = slotOf(slot);
  const int32_t entry = loaded.table.find(id.hash);
  return entry >= 0 && (loaded.table.flagsAt(entry) & ENTRY_FALLBACK);
}

void P64::Loc::setDebugMarks(bool fallbacks, bool missing)
{
  markFallbacks = fallbacks;
  markMissing = missing;
}

bool P64::Loc::marksFallbacks() { return markFallbacks; }
