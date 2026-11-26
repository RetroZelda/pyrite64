/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, sections 5 and 6). Compiles a text with [tags] and
// {variables} into the codes the console's formatter reads (engine text/locCodes.h), and checks it:
// an unknown tag, a bad value or an undeclared variable is an error.
#pragma once
#include <cstdint>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "localization.h"
#include "../../../n64/engine/include/text/locCodes.h"
#include "../../../n64/engine/include/text/locId.h"

namespace Project::Loc
{
  /** A control tag of locale.json "controls": "wait": "seconds". */
  struct ControlDef
  {
    std::string name{};
    std::string kind{"none"};           // seconds, number, name, none
    std::vector<std::string> names{};   // kind "name": every value used, in order of first use
  };

  /** A variable declared in C++ with P64_LOC_VAR or P64_LOC_ARG. */
  struct VarDecl
  {
    std::string type{"Int"}; // Int, Key, Text
    bool isArg{false};       // P64_LOC_ARG: passed by the caller
    std::string file{};
    int line{0};
  };
  using VarDecls = std::map<std::string, VarDecl>;

  /** A font set of locale.json "fontSets". */
  struct FontSetDef
  {
    std::string name{};
    std::string files[4]{};       // regular, bold, italic, boldItalic (paths relative to locale/)
    std::vector<std::string> varAxis[4]{}; // "wght=700" per variant, for variable fonts
    std::vector<int> sizes{16};
    int base{0};                  // index of the normal size
    float outline{0.0f};          // outline width in pixels, 0 for none
    uint32_t outlineColor{0x000000FFu};
    bool monochrome{false};
  };

  /** Everything a text may name, read from locale.json; compiling adds custom colors and control names. */
  struct CompileEnv
  {
    std::vector<std::string> palette{};     // [0] "default"; names, then custom "#rrggbb"
    std::vector<uint32_t> paletteRgba{};
    std::vector<FontSetDef> fontSets{};
    std::vector<std::string> effects{};     // [0] is "" (none)
    std::vector<ControlDef> controls{};     // op = index + 1
    std::vector<std::string> icons{};
    std::string iconSet{};                  // font set holding the icons
    uint32_t iconBase{0xE000};
    VarDecls vars{};
    std::vector<std::string> warnings{};

    [[nodiscard]] int fontSetIndex(const std::string &name) const;
    [[nodiscard]] int paletteIndex(const std::string &name) const;
  };

  /** Reads fontSets, colors, effects, controls and icons of locale.json. Problems go to 'errors'. */
  CompileEnv makeCompileEnv(const Config &config, const VarDecls &vars, std::vector<std::string> &errors);

  /** The plural rule of a language: its "plural" family or code, else its code. */
  P64::Loc::PluralRule pluralRuleOf(const Language &lang);
  /** Digit grouping of a language: separator (0 for none) and the fewest digits that are grouped. */
  std::pair<char, uint8_t> groupingOf(const Language &lang);

  /** Compiles one text. Errors are sentences without the key or language; the caller adds them. */
  std::string compileText(const std::string &source, CompileEnv &env, P64::Loc::PluralRule rule,
                          std::vector<std::string> &errors);

  /** Every glyph a compiled text shows (codes, tokens and line breaks left out). */
  void collectGlyphs(const std::string &compiled, std::set<uint32_t> &out);
  /** Whether a compiled text switches font set ([font=...]), so its glyphs belong to every set. */
  bool usesOtherFontSets(const std::string &compiled);

  /** Finds P64_LOC_VAR(name, Type) and P64_LOC_ARG(name, Type) in C++ source (comments skipped). */
  void scanVarDecls(const std::string &code, const std::string &file, VarDecls &out, std::vector<std::string> &errors);
}
