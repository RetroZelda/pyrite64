/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, sections 4.8 and 5).
#include "text/loc.h"
#include "text/locTable.h"

#include <libdragon.h>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "lib/logger.h"

namespace
{
  using namespace P64::Loc;

  struct Loaded
  {
    uint8_t lang{NO_LANGUAGE};
    void* file{};
    TableView table{};
  };

  const Language* languages{};
  uint8_t langCount{};
  const ProjectData* project{};
  Loaded slots[2]{};
  uint32_t langEpoch{};
  bool markFallbacks{};
  bool markMissing{};
  int8_t textSizeStep{};
  rdpq_font_t** loadedFonts{}; // per ProjectData::fonts entry

  // "#1234ABCD" for unknown ids when markMissing is on; a few at once stay valid
  char missingText[4][12]{};
  uint8_t missingNext{};

  bool loadSlot(Loaded &out, uint8_t lang)
  {
    int size = 0;
    void* file = asset_load(languages[lang].file, &size);
    if(!file) return false;
    TableView view{};
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

  const Loaded& slotOf(Slot slot)
  {
    return slots[slot == Slot::Paired ? 1 : 0];
  }

  uint8_t fontSetOf(uint8_t lang)
  {
    return lang < langCount ? languages[lang].fontSet : 0xFF;
  }

  void registerStyles(rdpq_font_t* font, const FontFile &file)
  {
    if(file.colored || !project) return;
    const uint32_t outline = file.set < project->setCount ? project->sets[file.set].outline : 0x000000FFu;
    for(uint8_t e = 0; e < (project->effectCount ? project->effectCount : 1); ++e) {
      for(uint8_t c = 0; c < project->paletteCount && c < 32; ++c) {
        rdpq_fontstyle_t style{};
        style.color = color_from_packed32(project->palette[c]);
        style.outline_color = color_from_packed32(outline);
        rdpq_font_style(font, styleId(c, e), &style);
      }
    }
  }

  /** Loads the fonts of the sets the two slots and the icons need; frees the others. */
  void syncFonts()
  {
    if(!project || project->fontCount == 0) return;
    if(!loadedFonts) {
      loadedFonts = static_cast<rdpq_font_t**>(calloc(project->fontCount, sizeof(rdpq_font_t*)));
      if(!loadedFonts) return;
    }
    const uint8_t want[3] = {fontSetOf(slots[0].lang), fontSetOf(slots[1].lang), project->iconSet};
    for(uint16_t i = 0; i < project->fontCount; ++i) {
      const FontFile &file = project->fonts[i];
      const bool needed = file.set == want[0] || file.set == want[1] || file.set == want[2];
      if(needed && !loadedFonts[i]) {
        rdpq_font_t* font = rdpq_font_load(file.path);
        if(!font) {
          P64::Log::error("Loc: cannot load the font %s", file.path);
          continue;
        }
        registerStyles(font, file);
        rdpq_text_register_font(file.id, font);
        loadedFonts[i] = font;
      } else if(!needed && loadedFonts[i]) {
        rdpq_text_unregister_font(file.id);
        rdpq_font_free(loadedFonts[i]);
        loadedFonts[i] = nullptr;
      }
    }
  }

  const char* keyTextOf(uint32_t id, bool* fallback, void* user)
  {
    const auto &loaded = *static_cast<const Loaded*>(user);
    const int32_t entry = loaded.table.find(id);
    if(entry < 0) return nullptr;
    if(fallback) *fallback = (loaded.table.flagsAt(entry) & ENTRY_FALLBACK) != 0;
    return loaded.table.textAt(entry);
  }

  FormatEnv envOf(const Loaded &loaded, const Args* args, int8_t sizeOffset = 0)
  {
    FormatEnv env{};
    if(project) {
      env.sets = project->sets;
      env.setCount = project->setCount;
      env.iconSet = project->iconSet;
      env.iconBase = project->iconBase;
      env.fallbackColor = markFallbacks ? project->fallbackColor : 0;
    }
    env.sizeStep = int8_t(textSizeStep + sizeOffset);
    if(loaded.lang < langCount) {
      const Language &lang = languages[loaded.lang];
      env.baseSet = lang.fontSet;
      env.plural = static_cast<PluralRule>(lang.plural);
      env.groupSep = lang.groupSep;
      env.groupMin = lang.groupMin;
    }
    env.args = args;
    env.keyText = keyTextOf;
    env.user = const_cast<Loaded*>(&loaded);
    return env;
  }

  int hexValue(char c)
  {
    if(c >= '0' && c <= '9') return c - '0';
    if(c >= 'a' && c <= 'f') return c - 'a' + 10;
    if(c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
  }
}

void P64::Loc::setLanguageList(const Language* list, uint8_t count)
{
  languages = list;
  langCount = count;
}

void P64::Loc::setProjectData(const ProjectData* data) { project = data; }
const P64::Loc::ProjectData* P64::Loc::projectData() { return project; }

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
  syncFonts();
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

P64::Loc::FormatResult P64::Loc::format(char* out, int size, LocId id, Slot slot, const Args* args, int8_t sizeOffset)
{
  const auto &loaded = slotOf(slot);
  const int32_t entry = loaded.table.find(id.hash);
  if(entry < 0) {
    const char* missing = text(id, slot);
    if(out && size > 0) {
      strncpy(out, missing, size_t(size) - 1);
      out[size - 1] = '\0';
    }
    return {static_cast<int>(out && size > 0 ? strlen(out) : 0), false, 0};
  }
  const bool fallback = (loaded.table.flagsAt(entry) & ENTRY_FALLBACK) != 0;
  return formatText(loaded.table.textAt(entry), fallback, out, size, envOf(loaded, args, sizeOffset));
}

P64::Loc::FormatResult P64::Loc::formatCompiled(char* out, int size, const char* compiled, bool fallback, Slot slot, const Args* args, int8_t sizeOffset)
{
  return formatText(compiled, fallback, out, size, envOf(slotOf(slot), args, sizeOffset));
}

uint8_t P64::Loc::fontFor(Slot slot, uint8_t variant, int8_t sizeOffset)
{
  const uint8_t set = fontSetOf(slotOf(slot).lang);
  if(!project || set >= project->setCount || variant >= VARIANTS) return 0;
  const FontSet &fs = project->sets[set];
  if(fs.sizeCount == 0) return 0;
  int sz = int(fs.baseSize) + textSizeStep + sizeOffset;
  if(sz < 0) sz = 0;
  if(sz >= fs.sizeCount) sz = fs.sizeCount - 1;
  return fs.ids[variant][sz] ? fs.ids[variant][sz] : fs.ids[0][sz];
}

void P64::Loc::setTextSize(int8_t steps) { textSizeStep = steps; }
int8_t P64::Loc::textSize() { return textSizeStep; }

uint8_t P64::Loc::baseFont(Slot slot)
{
  const uint8_t set = fontSetOf(slotOf(slot).lang);
  if(!project || set >= project->setCount) return 0;
  const FontSet &fs = project->sets[set];
  if(fs.sizeCount == 0) return 0;
  int sz = int(fs.baseSize) + textSizeStep;
  if(sz < 0) sz = 0;
  if(sz >= fs.sizeCount) sz = fs.sizeCount - 1;
  return fs.ids[0][sz];
}

P64::Loc::Scan P64::Loc::scanNext(const char*& p, uint8_t &font, uint8_t &style)
{
  Scan s{};
  while(*p) {
    s.at = p;
    const char c = p[0];
    if((c == '$' || c == '^') && p[1] != c) {
      const int hi = hexValue(p[1]);
      const int lo = hi < 0 ? -1 : hexValue(p[2]);
      if(lo >= 0) {
        if(c == '$') { font = uint8_t(hi * 16 + lo); style = 0; }
        else style = uint8_t(hi * 16 + lo);
        p += 3;
        continue;
      }
    }
    s.font = font;
    s.style = style;
    if((c == '$' || c == '^') && p[1] == c) {
      p += 2;
      s.codepoint = uint8_t(c);
    } else {
      s.codepoint = utf8Decode(p);
    }
    if(s.codepoint == '\n') { s.kind = Scan::Kind::Newline; return s; }
    if(isControl(s.codepoint)) {
      s.kind = Scan::Kind::Control;
      s.op = codeOp(s.codepoint);
      s.arg = codeArg(s.codepoint);
      return s;
    }
    const rdpq_font_t* fnt = font ? rdpq_text_get_font(font) : nullptr;
    rdpq_font_gmetrics_t m{};
    const uint32_t cp = s.codepoint == '\t' ? uint32_t(' ') : s.codepoint;
    if(!fnt || !rdpq_font_get_glyph_metrics(fnt, cp, &m)) continue; // rdpq skips what the font lacks
    s.kind = m.x1 == 0 ? Scan::Kind::Space : Scan::Kind::Glyph;
    return s;
  }
  s.kind = Scan::Kind::End;
  s.at = p;
  return s;
}

int P64::Loc::breakLines(char* text, int size, float width)
{
  if(!text) return 0;
  int len = static_cast<int>(strlen(text));

  // the font and style in effect at the start
  uint8_t font = 0, style = 0;
  {
    const char* p = text;
    const Scan first = scanNext(p, font, style);
    font = first.font;
    style = first.style;
  }
  if(!font || !rdpq_text_get_font(font)) return len;

  // one line of this font: a box a line and a half high lets rdpq lay out exactly one line
  float lineHeight = 0.0f;
  {
    rdpq_textparms_t parms{};
    parms.height = 1000;
    const char probe[] = "A\nA";
    int nbytes = 3;
    rdpq_paragraph_t* lay = rdpq_paragraph_build(&parms, font, probe, &nbytes);
    if(lay && lay->nchars >= 2) lineHeight = fabsf(float(lay->chars[1].y) - float(lay->chars[0].y));
    if(lay) rdpq_paragraph_free(lay);
  }
  if(lineHeight <= 0.0f) return len;

  int pos = 0;
  for(int guard = 0; guard < 256 && pos < len; ++guard) {
    rdpq_textparms_t parms{};
    parms.width = int16_t(width);
    parms.height = int16_t(lineHeight * 1.5f); // one line fits (a font's ascent and descent can exceed its line step), two do not
    parms.wrap = WRAP_CHAR;
    parms.style_id = style;
    int nbytes = len - pos;
    rdpq_paragraph_t* lay = rdpq_paragraph_build(&parms, font, text + pos, &nbytes);
    if(lay) rdpq_paragraph_free(lay);
    const int end = pos + nbytes;
    if(nbytes <= 0 || end >= len) break;

    // the line ended at a line break of its own: nothing to insert
    int brk = end;
    if(text[end - 1] != '\n') {
      // where each letter of this line starts, to step back over them
      constexpr int MAX_ITEMS = 96;
      const char* starts[MAX_ITEMS];
      uint32_t cps[MAX_ITEMS];
      int count = 0;
      uint8_t f = font, st = style;
      const char* p = text + pos;
      while(p < text + end && count < MAX_ITEMS) {
        const Scan it = scanNext(p, f, st);
        if(it.kind == Scan::Kind::End || it.at >= text + end) break;
        if(it.kind == Scan::Kind::Glyph || it.kind == Scan::Kind::Space) { starts[count] = it.at; cps[count] = it.codepoint; ++count; }
      }
      uint8_t f2 = f, st2 = st;
      const char* q = text + end;
      const Scan next = scanNext(q, f2, st2);
      // kinsoku: the next line must not start with a closing mark, this one must not end with an opening one
      int k = count; // index of the first letter of the next line
      uint32_t nextCp = next.codepoint;
      while(k > 1 && (kinsokuNoStart(nextCp) || kinsokuNoEnd(cps[k - 1]))) {
        --k;
        nextCp = cps[k];
      }
      brk = k < count ? int(starts[k] - text) : end;

      if(len + 2 > size) break; // no room for another line break
      memmove(text + brk + 1, text + brk, size_t(len - brk + 1));
      text[brk] = '\n';
      ++len;
    }

    // the font and style in effect at the new line's start
    {
      const char* p = text + pos;
      uint8_t f = font, st = style;
      while(p < text + brk) {
        const Scan it = scanNext(p, f, st);
        if(it.kind == Scan::Kind::End) break;
      }
      font = f;
      style = st;
    }
    pos = brk + (text[brk] == '\n' ? 1 : 0);
  }
  return len;
}

uint8_t P64::Loc::controlOp(const char* name)
{
  if(!project) return 0;
  for(uint8_t i = 0; i < project->controlCount; ++i) {
    if(strcmp(project->controls[i].name, name) == 0) return uint8_t(i + 1);
  }
  return 0;
}

const P64::Loc::ControlInfo* P64::Loc::control(uint8_t op)
{
  if(!project || op == 0 || op > project->controlCount) return nullptr;
  return &project->controls[op - 1];
}

bool P64::Loc::decodeControl(uint32_t codepoint, uint8_t &op, uint16_t &arg)
{
  if(!isControl(codepoint)) return false;
  op = codeOp(codepoint);
  arg = codeArg(codepoint);
  return true;
}

const char* P64::Loc::controlValueName(uint8_t op, uint16_t arg)
{
  const ControlInfo* ctl = control(op);
  if(!ctl || ctl->kind != CONTROL_NAME || arg >= ctl->nameCount) return "";
  return ctl->names[arg];
}

uint8_t P64::Loc::effectIndex(const char* name)
{
  if(!project) return 0;
  for(uint8_t i = 1; i < project->effectCount; ++i) {
    if(strcmp(project->effects[i], name) == 0) return i;
  }
  return 0;
}

void P64::Loc::setDebugMarks(bool fallbacks, bool missing)
{
  markFallbacks = fallbacks;
  markMissing = missing;
}

bool P64::Loc::marksFallbacks() { return markFallbacks; }
