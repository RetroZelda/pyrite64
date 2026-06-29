/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "projectBuilder.h"
#include "../utils/fs.h"
#include "../utils/string.h"
#include "../utils/logger.h"
#include "../project/canvas/canvas.h"

#include <filesystem>
#include <format>
#include <sstream>
#include <cctype>

namespace fs = std::filesystem;
using namespace Project;

namespace
{
    // =====================================================================
    // Repeater emit context (Phase 4) — set while generating a repeater subtree.
    // A file-scope context avoids threading these through every expression helper.
    // =====================================================================
    std::string g_itemRef;   // e.g. "data.Hearts_items[i]"  ("" outside a repeater)
    std::string g_offsetX;   // per-iteration x offset expr   ("" outside a repeater)
    std::string g_offsetY;   // per-iteration y offset expr
    std::string g_animRef;   // e.g. "data.__anim_12345" or item-local "data.X_items[i].__anim_12345"
    std::vector<uint64_t> g_animatedUuids;       // elements animated by the current canvas's clips
    std::vector<uint64_t> g_frameAnimatedUuids;  // elements with a FrameIndex track (current canvas)
    // Phase 5e: uuids animated by the element-scoped clips of the repeater we're currently inside.
    // For these, g_animRef resolves item-local (per-instance AnimState lives in items[i]).
    std::vector<uint64_t> g_repElemAnimUuids;

    bool isFrameAnimated(uint64_t uuid)
    {
        for (auto u : g_frameAnimatedUuids) if (u == uuid) return true;
        return false;
    }
    // Sprite-sheet frame grid authored via frameW/frameH(/frameCols) props.
    bool hasFrameGrid(const Project::CanvasElement& e) { return e.props.count("frameW") && e.props.count("frameH"); }
    // Wrap S/T authored via wrap_s/wrap_t props (0=Clamp,1=Repeat,2=Mirror).
    bool hasWrap(const Project::CanvasElement& e) { return e.props.count("wrap_s") || e.props.count("wrap_t"); }

    // Phase 5d: elements targeted by an Alpha / Color timeline track (current canvas).
    // The draw code composites these into prim color (+ a prim-aware combiner/blender).
    // Gated precisely so any element WITHOUT an Alpha/Color track emits byte-identical code.
    std::vector<uint64_t> g_alphaAnimatedUuids;
    std::vector<uint64_t> g_colorAnimatedUuids;
    bool isAlphaAnimated(uint64_t uuid) { for (auto u : g_alphaAnimatedUuids) if (u == uuid) return true; return false; }
    bool isColorAnimated(uint64_t uuid) { for (auto u : g_colorAnimatedUuids) if (u == uuid) return true; return false; }
    bool isTintAnimated(uint64_t uuid)  { return isAlphaAnimated(uuid) || isColorAnimated(uuid); }

    // Phase 5e: unique animated target uuids of ONE element's element-scoped clips.
    void collectElemClipAnimUuids(const Project::CanvasElement& e, std::vector<uint64_t>& out)
    {
        for (const auto& a : e.animations)
            for (const auto& t : a.tracks)
                if (t.channel != Project::CanvasTrackChannel::Variable && t.targetUuid != 0)
                {
                    bool seen = false;
                    for (auto u : out) if (u == t.targetUuid) { seen = true; break; }
                    if (!seen) out.push_back(t.targetUuid);
                }
    }

    // Sanitize an authored name into a valid C++ identifier.
    std::string sanitizeIdent(const std::string& s, size_t idx)
    {
        std::string out;
        for (char ch : s)
            out += (std::isalnum((unsigned char)ch) || ch == '_') ? ch : '_';
        if (out.empty())
            out = "Event" + std::to_string(idx);
        else if (std::isdigit((unsigned char)out[0]))
            out = "_" + out;
        return out;
    }

    // Base name for a repeater element's generated item struct + array.
    std::string repeaterBaseName(const Project::CanvasElement& e)
    {
        return sanitizeIdent(e.name, e.uuid & 0xFFFF);
    }

    // Build a deduped, sanitized enumerator list from names: "        <Name> = <idx>,\n".
    // The enumerator value IS the index, so the enum is interchangeable with the raw index.
    std::string enumBody(const std::vector<std::string>& names)
    {
        std::string out;
        std::vector<std::string> seen;
        for (size_t i = 0; i < names.size(); ++i) {
            std::string n = sanitizeIdent(names[i], i), base = n;
            int dup = 0;
            while (std::find(seen.begin(), seen.end(), n) != seen.end())
                n = base + "_" + std::to_string(++dup);
            seen.push_back(n);
            out += "        " + n + " = " + std::to_string(i) + ",\n";
        }
        return out;
    }

    std::string animEnumerators(const std::vector<Project::CanvasAnimation>& clips)
    {
        std::vector<std::string> names;
        for (const auto& a : clips) names.push_back(a.name);
        return enumBody(names);
    }

    // Live count expression for a repeater. Defaults to the generated `<base>_count` member
    // (set by gameplay in the model callback); a non-empty countVar overrides it with a
    // canvas variable (legacy). Either way `data.<base>_items[i]` is always emitted.
    std::string repeaterCountExpr(const Project::CanvasElement& e)
    {
        return e.repeater.countVar.empty()
            ? ("data." + repeaterBaseName(e) + "_count")
            : ("data." + e.repeater.countVar);
    }

    // Per-element AnimState member name in the Data struct (Phase 5).
    std::string animStateVar(uint64_t uuid)
    {
        return "__anim_" + std::to_string(uuid & 0xFFFFFFFFull);
    }

    // Emit prim color for a tint-animated element: composite the animated color[] (when the
    // Color channel is active) and multiply by the animated alpha. baseColor is the element's
    // authored color expr ("" -> opaque white). Only invoked when isTintAnimated(e.uuid).
    void emitAnimPrimColor(std::string& out, const std::string& indent,
                           const Project::CanvasElement& e, const std::string& baseColor)
    {
        // Use the resolved anim ref (flat "data.__anim_X" or item-local "...items[i].__anim_X").
        std::string a    = g_animRef.empty() ? ("data." + animStateVar(e.uuid)) : g_animRef;
        std::string base = baseColor.empty() ? "{0xFF,0xFF,0xFF,0xFF}" : baseColor;
        out += indent + "{ color_t __pc = " + base + ";\n";
        if (isColorAnimated(e.uuid))
            out += indent + "  if (" + a + ".hasColor) { __pc.r = " + a + ".color[0]; __pc.g = "
                 + a + ".color[1]; __pc.b = " + a + ".color[2]; __pc.a = " + a + ".color[3]; }\n";
        if (isAlphaAnimated(e.uuid))
            out += indent + "  __pc.a = (uint8_t)((float)__pc.a * " + a + ".alpha);\n";
        out += indent + "  rdpq_set_prim_color(__pc); }\n";
    }

    // If `p` is a repeater item-field / index binding, set `out` to its C++ expression.
    bool itemRefExpr(const Project::PropValue& p, std::string& out)
    {
        if (p.isIndex)     { out = g_itemRef.empty() ? "0" : "(int)i"; return true; }
        if (p.isItemField) { out = g_itemRef.empty() ? "0" : (g_itemRef + "." + p.itemField); return true; }
        return false;
    }

    // Add repeater per-iteration offset and animation dx/dy to an x / y base expression.
    std::string withOffsetX(const std::string& base)
    {
        std::string r = g_offsetX.empty() ? base : ("(" + base + ") + (" + g_offsetX + ")");
        if (!g_animRef.empty()) r = "(" + r + ") + (int)" + g_animRef + ".dx";
        return r;
    }
    std::string withOffsetY(const std::string& base)
    {
        std::string r = g_offsetY.empty() ? base : ("(" + base + ") + (" + g_offsetY + ")");
        if (!g_animRef.empty()) r = "(" + r + ") + (int)" + g_animRef + ".dy";
        return r;
    }

    // =====================================================================
    // Expression helpers
    // =====================================================================

    // Maps a CanvasVarType to the C++ default initializer for a data struct field
    std::string varDefaultInit(const CanvasVariableDef& v)
    {
        auto& d = v.defaultValue;
        // StringPtr stores its content without quotes; always wrap them.
        if (v.type == CanvasVarType::StringPtr)
            return d.empty() ? "\"\"" : "\"" + d + "\"";
        if (!d.empty()) return d;
        switch (v.type)
        {
            case CanvasVarType::Float:     return "0.0f";
            case CanvasVarType::Int:       return "0";
            case CanvasVarType::Bool:      return "false";
            case CanvasVarType::ColorRGBA: return "{0xFF, 0xFF, 0xFF, 0xFF}";
            case CanvasVarType::FontRef:   return "0";
            case CanvasVarType::SpriteRef: return "{}";
            default:                       return "{}";
        }
    }

    // Formats a float as a valid C++ float literal (always has a decimal point).
    std::string floatLit(float v)
    {
        auto s = std::format("{:g}", v);
        if (s.find('.') == std::string::npos && s.find('e') == std::string::npos)
            s += ".0";
        return s + "f";
    }

    // Resolves a PropValue to a C++ expression. dataRef is e.g. "data."
    // ident=true: string values are emitted unquoted (for macro/constant names like RDPQ_COMBINER_TEX).
    std::string propExpr(const PropValue& p, const std::string& literalFallback,
                         const std::string& dataRef = "data.", bool ident = false)
    {
        std::string ie;
        if (itemRefExpr(p, ie)) return ie;
        if (p.isBound) {
            if (p.isOp && !p.opLhs.empty() && !p.opOp.empty()) {
                std::string lhs = dataRef + p.opLhs;
                std::string rhs = p.opRhsIsVar ? (dataRef + p.opRhs) : p.opRhs;
                return "(" + lhs + " " + p.opOp + " " + rhs + ")";
            }
            if (!p.boundVar.empty())
                return dataRef + p.boundVar;
        }

        if (p.value.is_number_float())
            return floatLit(p.value.get<float>());
        if (p.value.is_number_integer())
            return std::to_string(p.value.get<int>());
        if (p.value.is_boolean())
            return p.value.get<bool>() ? "true" : "false";
        if (p.value.is_string())
        {
            auto s = p.value.get<std::string>();
            return ident ? s : ("\"" + s + "\"");
        }
        // Color stored as [r, g, b, a] array
        if (p.value.is_array() && p.value.size() == 4)
        {
            return "{" + std::to_string(p.value[0].get<int>()) + ", "
                       + std::to_string(p.value[1].get<int>()) + ", "
                       + std::to_string(p.value[2].get<int>()) + ", "
                       + std::to_string(p.value[3].get<int>()) + "}";
        }

        return literalFallback;
    }

    std::string propFloat(const PropValue& p, float def,
                          const std::string& dataRef = "data.")
    {
        std::string ie;
        if (itemRefExpr(p, ie)) return ie;
        if (p.isBound && !p.boundVar.empty()) return dataRef + p.boundVar;
        if (p.value.is_number()) return floatLit(p.value.get<float>());
        return floatLit(def);
    }

    // Emit an integer C++ expression from a PropValue.
    // Literal values are rounded and emitted as plain ints — no cast.
    // Variable-bound values keep a (int)() cast since the variable may be float.
    std::string posIntExpr(const PropValue& p, int def,
                           const std::string& dataRef = "data.")
    {
        std::string ie;
        if (itemRefExpr(p, ie)) return "(int)(" + ie + ")";
        if (p.isBound && !p.boundVar.empty())
            return "(int)(" + dataRef + p.boundVar + ")";
        if (p.value.is_number())
            return std::to_string((int)std::round(p.value.get<float>()));
        return std::to_string(def);
    }

    std::string getPropIntExpr(const CanvasElement& e, const std::string& key, int def,
                               const std::string& dataRef = "data.")
    {
        auto it = e.props.find(key);
        if (it == e.props.end()) return std::to_string(def);
        return posIntExpr(it->second, def, dataRef);
    }

    std::string propInt(const PropValue& p, int def,
                        const std::string& dataRef = "data.")
    {
        return propExpr(p, std::to_string(def), dataRef);
    }

    std::string getProp(const CanvasElement& e, const std::string& key,
                        const std::string& fallback = "", bool ident = false)
    {
        auto it = e.props.find(key);
        if (it == e.props.end()) return fallback;
        return propExpr(it->second, fallback, "data.", ident);
    }

    // A valid color_t expression for a color prop. Returns "" only when the prop is ABSENT
    // (caller emits no prim color). A present-but-malformed value (null, or a bare number — which
    // can't convert to color_t and previously caused `rdpq_set_prim_color(0)`) falls back to
    // opaque white, matching the historical null-color behaviour and keeping output byte-stable.
    std::string colorExpr(const CanvasElement& e, const char* key)
    {
        auto it = e.props.find(key);
        if (it == e.props.end()) return "";
        const auto& pv = it->second;
        if (pv.isBound && !pv.boundVar.empty()) return "data." + pv.boundVar;  // assumed color_t
        if (pv.value.is_array() && pv.value.size() >= 3) {
            int r = pv.value[0].get<int>(), g = pv.value[1].get<int>(), b = pv.value[2].get<int>();
            int a = pv.value.size() >= 4 && pv.value[3].is_number() ? pv.value[3].get<int>() : 255;
            return "{" + std::to_string(r) + ", " + std::to_string(g) + ", "
                       + std::to_string(b) + ", " + std::to_string(a) + "}";
        }
        return "{0xFF,0xFF,0xFF,0xFF}";  // present but null/number/malformed -> safe opaque white
    }

    std::string getPropFloat(const CanvasElement& e, const std::string& key,
                             float def, const std::string& dataRef = "data.")
    {
        auto it = e.props.find(key);
        if (it == e.props.end()) return floatLit(def);
        return propFloat(it->second, def, dataRef);
    }

    std::string getPropInt(const CanvasElement& e, const std::string& key,
                           int def, const std::string& dataRef = "data.")
    {
        auto it = e.props.find(key);
        if (it == e.props.end()) return std::to_string(def);
        return propInt(it->second, def, dataRef);
    }

    // Unique static var name for a baked sprite on an element
    std::string bakedSpriteVarName(const CanvasElement& e)
    {
        return "s_spr_" + std::to_string(e.uuid & 0xFFFFFFFFull);
    }

    // Returns a sprite get-expression — variable binding, baked static, or nullptr
    std::string spriteExpr(const CanvasElement& e, const std::string& key)
    {
        auto it = e.props.find(key);
        if (it == e.props.end()) return "nullptr";
        const auto& p = it->second;
        if (p.isBound && !p.boundVar.empty())
            return "data." + p.boundVar + ".get()";
        // Baked literal UUID → static local AssetRef generated in draw function preamble
        if (p.value.is_number_integer() && p.value.get<uint64_t>() != 0)
            return bakedSpriteVarName(e) + ".get()";
        return "nullptr";
    }

    std::string fontExpr(const CanvasElement& e, const std::string& key)
    {
        auto it = e.props.find(key);
        if (it == e.props.end()) return "nullptr";
        const auto& p = it->second;
        if (p.isBound && !p.boundVar.empty())
            return "data." + p.boundVar + ".get()";
        return "nullptr";
    }

    // Emit static AssetRef declarations for all baked (non-variable) sprite props on an element
    void emitBakedSprites(std::string& out, const CanvasElement& e,
                          AssetManager& assets)
    {
        for (const auto* key : {"sprite"})
        {
            auto it = e.props.find(key);
            if (it == e.props.end()) continue;
            const auto& p = it->second;
            if (p.isBound || !p.value.is_number_integer()) continue;
            uint64_t uuid = p.value.get<uint64_t>();
            if (uuid == 0) continue;
            auto* entry = assets.getEntryByUUID(uuid);
            if (!entry || entry->romPath.empty()) continue;
            auto romPath = entry->romPath.substr(5); // strip "rom:/"
            // NOT static: a function-local static AssetRef caches the resolved
            // pointer permanently, which dangles after a scene reloads its assets
            // (the classic scene-switch UI crash). A fresh local re-resolves the
            // index via the asset manager each frame (cheap table lookup).
            out += "    P64::AssetRef<sprite_t> " + bakedSpriteVarName(e)
                 + "{(sprite_t*)(uintptr_t)(\"" + romPath + "\"_asset)};\n";
        }
        for (const auto& child : e.children)
            emitBakedSprites(out, child, assets);
    }

    // Phase 0 seam: an element subtree is "static" iff nothing about it varies
    // at runtime (no bound/op props anywhere). Later phases extend this
    // (focusable/scripted/timeline-targeted/repeater/item-index => dynamic).
    // The Phase 6 rspq_block optimization keys off this; a missed case would
    // bake a dynamic element into a frozen recorded block.
    [[maybe_unused]] bool isElementStatic(const CanvasElement& e)
    {
        if (e.x.isBound || e.y.isBound || e.rotation.isBound || e.visible.isBound)
            return false;
        for (const auto& [key, p] : e.props)
            if (p.isBound) return false;
        for (const auto& child : e.children)
            if (!isElementStatic(child)) return false;
        return true;
    }

    // =====================================================================
    // Per-element draw code generators
    // =====================================================================

    void generateSpriteCode(std::string& out, const CanvasElement& e)
    {
        auto x       = withOffsetX(posIntExpr(e.x, 0));
        auto y       = withOffsetY(posIntExpr(e.y, 0));
        auto scaleX  = getPropFloat(e, "blit_scale_x", 1.0f);
        auto scaleY  = getPropFloat(e, "blit_scale_y", 1.0f);
        auto blend   = getProp(e, "blendMode",  "0", /*ident=*/true);
        auto alpha   = getPropInt(e,  "alphaCompare", 0);
        auto sprite  = spriteExpr(e, "sprite");

        // Combiner is a C macro name — emit unquoted (ident=true)
        bool hasCombiner = e.props.count("combiner");
        std::string combiner = "RDPQ_COMBINER_TEX";
        if (hasCombiner) {
            auto it = e.props.find("combiner");
            combiner = propExpr(it->second, "RDPQ_COMBINER_TEX", "data.", /*ident=*/true);
        }

        std::string color = colorExpr(e, "color");
        bool hasColor = !color.empty();

        // Phase 5d: an Alpha/Color-animated sprite needs a prim-aware combiner (TEX*PRIM)
        // and an alpha-over blender so the animated prim color tints/fades it.
        bool tint = isTintAnimated(e.uuid);
        std::string useBlend = tint ? std::string("RDPQ_BLENDER_MULTIPLY")  : blend;
        std::string useComb  = tint ? std::string("RDPQ_COMBINER_TEX_FLAT") : combiner;

        out += "    // " + e.name + " [Sprite]\n";
        out += "    if (" + sprite + ") {\n";
        out += "        rdpq_mode_begin();\n";
        out += "          rdpq_mode_blender(" + useBlend + ");\n";
        if (!alpha.empty() && alpha != "0")
            out += "          rdpq_mode_alphacompare(" + alpha + ");\n";
        out += "          rdpq_mode_combiner(" + useComb + ");\n";
        out += "        rdpq_mode_end();\n";
        if (tint)
            emitAnimPrimColor(out, "        ", e, color);
        else if (hasColor && !color.empty())
            out += "        rdpq_set_prim_color(" + color + ");\n";
        auto id = std::to_string(e.uuid & 0xFFFF);
        auto p  = "__p_" + id;
        out += "        rdpq_blitparms_t " + p + "{};\n";

        // Transform-derived fields (composite animation scale/rotation when animated)
        bool animated = !g_animRef.empty();
        std::string scaleXe = animated ? ("(" + scaleX + ") * " + g_animRef + ".scale") : scaleX;
        std::string scaleYe = animated ? ("(" + scaleY + ") * " + g_animRef + ".scale") : scaleY;
        out += "        " + p + ".scale_x = " + scaleXe + ";\n";
        out += "        " + p + ".scale_y = " + scaleYe + ";\n";
        bool hasRot = e.rotation.isBound ||
            (e.rotation.value.is_number() && e.rotation.value.get<float>() != 0.0f);
        if (hasRot || animated) {
            std::string thetaBase = propFloat(e.rotation, 0.0f);
            out += "        " + p + ".theta = "
                 + (animated ? ("(" + thetaBase + ") + " + g_animRef + ".drot") : thetaBase) + ";\n";
        }

        // Blit-specific fields — only emit when non-default or bound
        auto emitBlitInt = [&](const char* prop, const char* field, int def) {
            auto it = e.props.find(prop);
            if (it == e.props.end()) return;
            const auto& pv = it->second;
            if (!pv.isBound && pv.value.is_number() && (int)pv.value.get<float>() == def) return;
            if (!pv.isBound && !pv.value.is_number()) return;
            out += "        " + p + "." + field + " = " + posIntExpr(pv, def) + ";\n";
        };
        auto emitBlitBool = [&](const char* prop, const char* field) {
            auto it = e.props.find(prop);
            if (it == e.props.end()) return;
            const auto& pv = it->second;
            if (!pv.isBound && pv.value.is_boolean() && !pv.value.get<bool>()) return;
            if (!pv.isBound && !pv.value.is_boolean()) return;
            out += "        " + p + "." + field + " = " + propExpr(pv, "false") + ";\n";
        };

        emitBlitInt("blit_tile",   "tile",        0);
        emitBlitInt("blit_s0",     "s0",          0);
        emitBlitInt("blit_t0",     "t0",          0);
        emitBlitInt("blit_w",      "width",       0);
        emitBlitInt("blit_h",      "height",      0);
        emitBlitBool("blit_flip_x", "flip_x");
        emitBlitBool("blit_flip_y", "flip_y");
        emitBlitInt("blit_cx",     "cx",          0);
        emitBlitInt("blit_cy",     "cy",          0);
        emitBlitBool("blit_allow_xform", "allow_xform");
        emitBlitBool("blit_filtering",   "filtering");

        // Sprite-sheet frame (Phase 5c): override the blit sub-rect from a frame grid.
        // Frame index comes from a FrameIndex animation track, else the (bindable) frameIndex prop.
        if (hasFrameGrid(e))
        {
            auto fw   = getPropInt(e, "frameW",    16);
            auto fh   = getPropInt(e, "frameH",    16);
            auto cols = getPropInt(e, "frameCols", 1);
            std::string fi = isFrameAnimated(e.uuid)
                ? ("(int)" + (g_animRef.empty() ? "data." + animStateVar(e.uuid) : g_animRef) + ".frameIndex")
                : getPropInt(e, "frameIndex", 0);
            out += "        { auto __fr = P64::UI::Eval::frameRect(" + fi + ", " + fw + ", " + fh + ", " + cols + ");\n";
            out += "          " + p + ".s0 = __fr.x; " + p + ".t0 = __fr.y; "
                 + p + ".width = " + fw + "; " + p + ".height = " + fh + "; }\n";
        }

        out += "        rdpq_sprite_blit(" + sprite + ", " + x + ", " + y + ", &" + p + ");\n";
        out += "    }\n";
    }

    void generateTextCode(std::string& out, const CanvasElement& e)
    {
        auto x     = withOffsetX(posIntExpr(e.x, 0));
        auto yRaw  = withOffsetY(posIntExpr(e.y, 0));
        // rdpq_text uses y as the baseline (bottom of letter bodies), so offset
        // by the paragraph height so text fills the box from the top down.
        auto tpHIt = e.props.find("tp_height");
        int  tpH   = (tpHIt != e.props.end() && !tpHIt->second.isBound &&
                    tpHIt->second.value.is_number())
                    ? tpHIt->second.value.get<int>() : 0;
        auto y     = tpH > 0 ? ("(" + yRaw + " + " + std::to_string(tpH) + ")") : yRaw + " + 8";
        auto scale = getPropFloat(e, "scale", 1.0f);
        auto style = getPropInt(e, "style", 0);
        std::string color = colorExpr(e, "color");
        bool hasColor = !color.empty();

        // Format string: literal or variable-bound
        std::string textExpr;
        auto textIt = e.props.find("text");
        if (textIt != e.props.end()) {
            const auto& p = textIt->second;
            if (p.isBound && !p.boundVar.empty())
                textExpr = "data." + p.boundVar;
            else if (p.value.is_string())
                textExpr = "\"" + p.value.get<std::string>() + "\"";
            else
                textExpr = "\"\"";
        } else {
            textExpr = "\"\"";
        }

        // Printf arguments: stored as JSON array of variable name strings in props["args"]
        std::vector<std::string> argExprs;
        auto argsIt = e.props.find("args");
        if (argsIt != e.props.end() && argsIt->second.value.is_array()) {
            for (const auto& arg : argsIt->second.value) {
                if (arg.is_string() && !arg.get<std::string>().empty())
                    argExprs.push_back("data." + arg.get<std::string>());
            }
        }

        auto fontIdExpr = getPropInt(e, "font", 0);

        auto id = std::to_string(e.uuid & 0xFFFF);
        std::string tp = "__tp_" + id;

        // Collect non-default textparms fields
        std::string tpFields;
        auto emitTpInt = [&](const char* field, const char* prop) {
            auto it = e.props.find(prop);
            if (it == e.props.end()) return;
            const auto& p = it->second;
            if (p.isBound && !p.boundVar.empty()) {
                tpFields += "        " + tp + "." + field + " = (int16_t)data." + p.boundVar + ";\n";
            } else {
                int v = p.value.is_number() ? p.value.get<int>() : 0;
                if (v != 0) tpFields += "        " + tp + "." + field + " = " + std::to_string(v) + ";\n";
            }
        };
        auto emitTpEnum = [&](const char* field, const char* prop,
                               std::initializer_list<const char*> names) {
            auto it = e.props.find(prop);
            if (it == e.props.end()) return;
            const auto& p = it->second;
            if (p.isBound && !p.boundVar.empty()) {
                tpFields += "        " + tp + "." + field + " = (decltype(" + tp + "." + field + "))data." + p.boundVar + ";\n";
                return;
            }
            int v = p.value.is_number() ? p.value.get<int>() : 0;
            if (v == 0) return;
            int n = (int)names.size();
            if (v >= 0 && v < n) tpFields += "        " + tp + "." + field + " = " + *(names.begin() + v) + ";\n";
        };
        auto emitTpBool = [&](const char* field, const char* prop) {
            auto it = e.props.find(prop);
            if (it == e.props.end()) return;
            const auto& p = it->second;
            if (p.isBound && !p.boundVar.empty()) {
                tpFields += "        " + tp + "." + field + " = (bool)data." + p.boundVar + ";\n";
                return;
            }
            bool v = p.value.is_boolean() && p.value.get<bool>();
            if (v) tpFields += "        " + tp + "." + field + " = true;\n";
        };

        emitTpInt("style_id",     "style");
        emitTpInt("width",        "tp_width");
        emitTpInt("height",       "tp_height");
        emitTpEnum("align",       "tp_align",  {"ALIGN_LEFT", "ALIGN_CENTER", "ALIGN_RIGHT"});
        emitTpEnum("valign",      "tp_valign", {"VALIGN_TOP", "VALIGN_CENTER", "VALIGN_BOTTOM"});
        emitTpInt("indent",       "tp_indent");
        emitTpInt("max_chars",    "tp_max_chars");
        emitTpInt("char_spacing", "tp_char_spacing");
        emitTpInt("line_spacing", "tp_line_spacing");
        emitTpEnum("wrap",        "tp_wrap",   {"WRAP_NONE", "WRAP_ELLIPSES", "WRAP_CHAR", "WRAP_WORD"});
        emitTpBool("disable_aa_fix",   "tp_disable_aa_fix");
        emitTpBool("preserve_overlap", "tp_preserve_overlap");

        out += "    // " + e.name + " [Text]\n";
        out += "    {\n";
        // Phase 5d: rdpq_text uses PRIM for glyph color, so an animated prim tints (and,
        // where the text combiner honours prim alpha, fades) the text.
        if (isTintAnimated(e.uuid))
            emitAnimPrimColor(out, "        ", e, color);
        else if (hasColor && !color.empty())
            out += "        rdpq_set_prim_color(" + color + ");\n";
        if (tpFields.empty()) {
            out += "        rdpq_text_printf(NULL, " + fontIdExpr + ", " + x + ", " + y + ", " + textExpr;
        } else {
            out += "        rdpq_textparms_t " + tp + "{};\n";
            out += tpFields;
            out += "        rdpq_text_printf(&" + tp + ", " + fontIdExpr + ", " + x + ", " + y + ", " + textExpr;
        }
        for (const auto& arg : argExprs)
            out += ", " + arg;
        out += ");\n";
        out += "    }\n";
    }

    void generateColorRectCode(std::string& out, const CanvasElement& e)
    {
        auto x      = withOffsetX(posIntExpr(e.x, 0));
        auto y      = withOffsetY(posIntExpr(e.y, 0));
        auto w      = getPropFloat(e, "width",  32.0f);
        auto h      = getPropFloat(e, "height", 32.0f);
        std::string color = colorExpr(e, "color");
        if (color.empty()) color = "{0xFF,0xFF,0xFF,0xFF}";   // ColorRect always fills

        // Phase 5d: a tint-animated solid rect needs an alpha-over blender (FILL can't blend,
        // but the FLAT combiner already runs in standard mode here).
        bool tint = isTintAnimated(e.uuid);

        out += "    // " + e.name + " [ColorRect]\n";
        if (tint)
            emitAnimPrimColor(out, "    ", e, color);
        else
            out += "    rdpq_set_prim_color(" + color + ");\n";
        out += "    rdpq_mode_begin();\n";
        if (tint)
            out += "      rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);\n";
        out += "      rdpq_mode_combiner(RDPQ_COMBINER_FLAT);\n";
        out += "    rdpq_mode_end();\n";
        out += "    rdpq_fill_rectangle(" + x + ", " + y
             + ", (" + x + ") + (" + w + "), (" + y + ") + (" + h + "));\n";
    }

    void generateTextureRectCode(std::string& out, const CanvasElement& e)
    {
        auto x   = withOffsetX(posIntExpr(e.x, 0));
        auto y   = withOffsetY(posIntExpr(e.y, 0));
        auto w   = getPropIntExpr(e, "width",  32);
        auto h   = getPropIntExpr(e, "height", 32);
        auto s0  = getPropIntExpr(e, "s0", 0);
        auto t0  = getPropIntExpr(e, "t0", 0);
        auto sprite = spriteExpr(e, "sprite");
        std::string color = colorExpr(e, "color");
        bool hasColor = !color.empty();

        // Sprite-sheet frame (Phase 5c): override s0/t0 (+ w/h) from a frame grid.
        bool frameGrid = hasFrameGrid(e);
        std::string fiExpr;
        if (frameGrid)
            fiExpr = isFrameAnimated(e.uuid)
                ? ("(int)" + (g_animRef.empty() ? "data." + animStateVar(e.uuid) : g_animRef) + ".frameIndex")
                : getPropInt(e, "frameIndex", 0);

        // Wrap S/T (Phase 5c): 0=Clamp, 1=Repeat, 2=Mirror -> rdpq_texparms_t.
        bool wrap = hasWrap(e);
        auto wrapRepeats = [&](const char* key) {
            int m = 0; auto it = e.props.find(key);
            if (it != e.props.end() && !it->second.isBound && it->second.value.is_number())
                m = it->second.value.get<int>();
            return m == 0 ? std::string("1") : std::string("REPEAT_INFINITE");
        };
        auto wrapMirror = [&](const char* key) {
            int m = 0; auto it = e.props.find(key);
            if (it != e.props.end() && !it->second.isBound && it->second.value.is_number())
                m = it->second.value.get<int>();
            return m == 2;
        };

        // Phase 5d: tint/fade via a prim-aware combiner + alpha-over blender when animated.
        bool tint = isTintAnimated(e.uuid);

        out += "    // " + e.name + " [TextureRect]\n";
        out += "    if (" + sprite + ") {\n";
        out += "        rdpq_mode_begin();\n";
        if (tint)
            out += "          rdpq_mode_blender(RDPQ_BLENDER_MULTIPLY);\n";
        out += "          rdpq_mode_combiner(" + std::string(tint ? "RDPQ_COMBINER_TEX_FLAT" : "RDPQ_COMBINER_TEX") + ");\n";
        out += "        rdpq_mode_end();\n";
        if (tint)
            emitAnimPrimColor(out, "        ", e, color);
        else if (hasColor && !color.empty())
            out += "        rdpq_set_prim_color(" + color + ");\n";
        if (wrap) {
            out += "        rdpq_texparms_t __tp{};\n";
            out += "        __tp.s.repeats = " + wrapRepeats("wrap_s") + "; __tp.s.mirror = "
                 + (wrapMirror("wrap_s") ? "true" : "false") + ";\n";
            out += "        __tp.t.repeats = " + wrapRepeats("wrap_t") + "; __tp.t.mirror = "
                 + (wrapMirror("wrap_t") ? "true" : "false") + ";\n";
            out += "        rdpq_sprite_upload(TILE0, " + sprite + ", &__tp);\n";
        } else {
            out += "        rdpq_sprite_upload(TILE0, " + sprite + ", nullptr);\n";
        }
        if (frameGrid) {
            auto fw = getPropInt(e, "frameW", 16), fh = getPropInt(e, "frameH", 16), cols = getPropInt(e, "frameCols", 1);
            out += "        { auto __fr = P64::UI::Eval::frameRect(" + fiExpr + ", " + fw + ", " + fh + ", " + cols + ");\n";
            out += "        rdpq_texture_rectangle(TILE0, " + x + ", " + y + ", "
                 + x + " + " + fw + ", " + y + " + " + fh + ", __fr.x, __fr.y); }\n";
        } else {
            out += "        rdpq_texture_rectangle(TILE0, " + x + ", " + y + ", "
                 + x + " + " + w + ", " + y + " + " + h + ", "
                 + s0 + ", " + t0 + ");\n";
        }
        out += "    }\n";
    }

    void generateElementCode(std::string& out, const CanvasElement& e)
    {
        // Literal false: skip entirely
        if (!e.visible.isBound) {
            bool vis = e.visible.value.is_boolean() ? e.visible.value.get<bool>() : true;
            if (!vis) return;
        }

        // Repeater (Phase 4): wrap this subtree in a count-bounded loop and set the
        // emit context so item-field/index props resolve and positions get an offset.
        const bool isRep = e.repeater.enabled;
        std::string savedItemRef = g_itemRef, savedOX = g_offsetX, savedOY = g_offsetY;
        std::string savedAnim = g_animRef;
        std::vector<uint64_t> savedRepElem = g_repElemAnimUuids;

        if (isRep)
        {
            int cols = e.repeater.columns < 1 ? 1 : e.repeater.columns;
            out += "    for (uint32_t i = 0; i < (uint32_t)" + repeaterCountExpr(e)
                 + " && i < " + std::to_string(e.repeater.maxCount) + "u; ++i) {\n";
            g_itemRef = "data." + repeaterBaseName(e) + "_items[i]";
            g_offsetX = "(int)((i % " + std::to_string(cols) + "u) * "
                      + std::to_string((int)e.repeater.spacingX) + ")";
            g_offsetY = "(int)((i / " + std::to_string(cols) + "u) * "
                      + std::to_string((int)e.repeater.spacingY) + ")";
            // Phase 5e: this repeater's element-scoped clips animate its subtree per-instance.
            g_repElemAnimUuids.clear();
            collectElemClipAnimUuids(e, g_repElemAnimUuids);
        }

        // Resolve THIS element's AnimState ref fresh (no inheritance from a parent — matches the
        // editor's per-element preview). A per-instance (item-local) ref from an enclosing
        // repeater's element clips takes priority over a canvas-clip flat AnimState.
        g_animRef.clear();
        {
            bool itemAnim = false;
            for (auto u : g_repElemAnimUuids) if (u == e.uuid) { itemAnim = true; break; }
            if (itemAnim && !g_itemRef.empty())
                g_animRef = g_itemRef + "." + animStateVar(e.uuid);
            else
                for (auto u : g_animatedUuids)
                    if (u == e.uuid) { g_animRef = "data." + animStateVar(e.uuid); break; }
        }

        bool hasVisGuard = e.visible.isBound;
        if (hasVisGuard)
            out += "    if (" + propExpr(e.visible, "true") + ") {\n";

        switch (e.type)
        {
            case CanvasElementType::Sprite:      generateSpriteCode(out, e);      break;
            case CanvasElementType::Text:        generateTextCode(out, e);        break;
            case CanvasElementType::ColorRect:   generateColorRectCode(out, e);   break;
            case CanvasElementType::TextureRect: generateTextureRectCode(out, e); break;
        }

        for (const auto& child : e.children)
            generateElementCode(out, child);

        if (hasVisGuard)
            out += "    }\n";

        if (isRep)
        {
            out += "    }\n";
            g_itemRef = savedItemRef; g_offsetX = savedOX; g_offsetY = savedOY;
        }
        g_animRef = savedAnim;
        g_repElemAnimUuids = savedRepElem;
    }

    // =====================================================================
    // Main generators
    // =====================================================================

    // Returns the C++ default initializer for a sprite variable given its default asset path
    std::string spriteVarInit(const std::string& defaultValue)
    {
        if (defaultValue.empty() || defaultValue == "{}") return "{}";
        return "{(sprite_t*)(uintptr_t)(\"" + defaultValue + "\"_asset)}";
    }

    // Returns true if any canvas has a sprite variable with a non-empty asset path default
    bool needsAssetTable(const std::vector<const Canvas*>& canvases)
    {
        for (const auto* c : canvases)
            for (const auto& v : c->variables)
                if (v.type == CanvasVarType::SpriteRef && !v.defaultValue.empty()
                    && v.defaultValue != "{}")
                    return true;
        return false;
    }

    // A UI element carrying a component script (Phase 3).
    struct ScriptedElem
    {
        uint32_t scriptIdx;  // index into the generated ScriptEntry table
        bool     focusable;  // participates in focus navigation
    };

    bool elemFocusable(const CanvasElement& e)
    {
        auto it = e.props.find("focusable");
        return it != e.props.end() && !it->second.isBound
            && it->second.value.is_boolean() && it->second.value.get<bool>();
    }

    // Walk the tree collecting repeater-enabled elements (count from data.<base>_count).
    void collectRepeaters(const CanvasElement& e, std::vector<const CanvasElement*>& out)
    {
        if (e.repeater.enabled)
            out.push_back(&e);
        for (const auto& c : e.children)
            collectRepeaters(c, out);
    }

    // AnimState field for an element channel ("" for Variable/Color which are special).
    const char* channelField(CanvasTrackChannel ch)
    {
        switch (ch)
        {
            case CanvasTrackChannel::X:          return "dx";
            case CanvasTrackChannel::Y:          return "dy";
            case CanvasTrackChannel::Rotation:   return "drot";
            case CanvasTrackChannel::Scale:      return "scale";
            case CanvasTrackChannel::Alpha:      return "alpha";
            case CanvasTrackChannel::FrameIndex: return "frameIndex";
            default:                             return ""; // Variable, Color (handled elsewhere)
        }
    }

    void collectFrameAnimatedUuids(const Canvas& c, std::vector<uint64_t>& out)
    {
        for (const auto& a : c.animations)
            for (const auto& t : a.tracks)
                if (t.channel == CanvasTrackChannel::FrameIndex && t.targetUuid != 0)
                    out.push_back(t.targetUuid);
    }

    void collectTintAnimatedUuids(const Canvas& c, std::vector<uint64_t>& alphaOut,
                                  std::vector<uint64_t>& colorOut)
    {
        for (const auto& a : c.animations)
            for (const auto& t : a.tracks) {
                if (t.targetUuid == 0) continue;
                if (t.channel == CanvasTrackChannel::Alpha) alphaOut.push_back(t.targetUuid);
                if (t.channel == CanvasTrackChannel::Color) colorOut.push_back(t.targetUuid);
            }
    }

    // Collect uuids of elements targeted by any canvas-clip element-channel track.
    void collectAnimatedUuids(const Canvas& c, std::vector<uint64_t>& out)
    {
        for (const auto& a : c.animations)
            for (const auto& t : a.tracks)
                if (t.channel != CanvasTrackChannel::Variable && t.targetUuid != 0)
                {
                    bool seen = false;
                    for (auto u : out) if (u == t.targetUuid) { seen = true; break; }
                    if (!seen) out.push_back(t.targetUuid);
                }
    }

    // Phase 5e: OR element-clip channel targets (across the whole tree) into the tint/frame
    // sets, so per-instance draws composite alpha/color/frame just like canvas clips do.
    void collectElemClipChannelUuids(const CanvasElement& e,
        std::vector<uint64_t>& alphaOut, std::vector<uint64_t>& colorOut, std::vector<uint64_t>& frameOut)
    {
        // Only repeater elements get per-instance AnimState (in items[i]); element clips on a
        // non-repeater element are unsupported and ignored, so don't mark their targets tint/frame-
        // animated (that would emit emitAnimPrimColor against a flat AnimState that doesn't exist).
        if (e.repeater.enabled)
            for (const auto& a : e.animations)
                for (const auto& t : a.tracks) {
                    if (t.targetUuid == 0) continue;
                    if (t.channel == CanvasTrackChannel::Alpha)      alphaOut.push_back(t.targetUuid);
                    if (t.channel == CanvasTrackChannel::Color)      colorOut.push_back(t.targetUuid);
                    if (t.channel == CanvasTrackChannel::FrameIndex) frameOut.push_back(t.targetUuid);
                }
        for (const auto& c : e.children)
            collectElemClipChannelUuids(c, alphaOut, colorOut, frameOut);
    }

    // Repeater elements (in tree order) that own element-scoped clips (Phase 5e).
    void collectElemClipRepeaters(const CanvasElement& e, std::vector<const CanvasElement*>& out)
    {
        if (e.repeater.enabled && !e.animations.empty())
            out.push_back(&e);
        for (const auto& c : e.children)
            collectElemClipRepeaters(c, out);
    }

    // Index of a named event in the canvas event list (-1 if not found).
    int eventIndex(const Canvas& c, const std::string& name)
    {
        for (size_t i = 0; i < c.events.size(); ++i)
            if (c.events[i].name == name) return (int)i;
        return -1;
    }

    // Walk the tree collecting elements that reference a resolvable script.
    void collectScriptedElements(const CanvasElement& e,
                                 const std::unordered_map<uint64_t, uint32_t>& codeIdx,
                                 std::vector<ScriptedElem>& out)
    {
        if (e.scriptUuid != 0)
        {
            auto it = codeIdx.find(e.scriptUuid);
            if (it != codeIdx.end())
                out.push_back({it->second, elemFocusable(e)});
        }
        for (const auto& c : e.children)
            collectScriptedElements(c, codeIdx, out);
    }

    void generateUISceneTypes(const std::vector<const Canvas*>& canvases,
                              const std::unordered_map<uint64_t, uint32_t>& codeIdx,
                              const std::string& outPath)
    {
        // Do any canvases use scripts or animations? (Determines header include.)
        bool needsUiHeader = false;
        for (const auto* c : canvases)
        {
            std::vector<ScriptedElem> se;
            for (const auto& e : c->elements) collectScriptedElements(e, codeIdx, se);
            if (!se.empty() || !c->animations.empty()) { needsUiHeader = true; break; }
        }

        std::string src;
        src += "// NOTE: Auto-Generated File!\n";
        src += "#pragma once\n";
        src += "#include <cstdint>\n";
        src += "#include <assets/assetManager.h>\n";
        if (needsUiHeader)
            src += "#include \"ui/uiElemState.h\"\n";
        if (needsAssetTable(canvases))
            src += "#include \"assetTable.h\"\n";
        src += "\n";

        // Emit data structs
        src += "namespace P64::UI::Types\n{\n";
        for (const auto* c : canvases)
        {
            src += "    struct " + c->name + "Data\n    {\n";
            for (const auto& v : c->variables)
            {
                std::string init;
                if (v.type == CanvasVarType::SpriteRef)
                    init = spriteVarInit(v.defaultValue);
                else
                    init = varDefaultInit(v);
                src += "        " + std::string(Canvas::varTypeToCpp(v.type)) + " "
                     + v.name + "{" + init + "};\n";
            }

            // Repeater item arrays (Phase 4): a per-item struct + fixed array + count.
            // Gameplay fills count + items[0..count) each frame.
            std::vector<const CanvasElement*> reps;
            for (const auto& e : c->elements) collectRepeaters(e, reps);
            for (const auto* re : reps)
            {
                auto base = repeaterBaseName(*re);
                src += "        struct " + base + "Item\n        {\n";
                for (const auto& f : re->repeater.itemFields)
                {
                    if (f.type == CanvasVarType::SpriteRef) continue; // forbidden (atlas-safety)
                    src += "            " + std::string(Canvas::varTypeToCpp(f.type)) + " "
                         + f.name + "{};\n";
                }
                // Phase 5e: per-instance animation. Each item carries its own AnimState for every
                // element-clip-animated target + its own playback (one per element clip).
                if (!re->animations.empty())
                {
                    std::vector<uint64_t> elemAnim;
                    collectElemClipAnimUuids(*re, elemAnim);
                    src += "            // per-instance element-clip animation (Phase 5e)\n";
                    for (auto u : elemAnim)
                        src += "            P64::UI::AnimState " + animStateVar(u) + "{};\n";
                    src += "            P64::UI::AnimPlayback __pb["
                         + std::to_string(re->animations.size()) + "]{};\n";
                    // This item's element clips, scoped to the item it animates.
                    src += "            enum class Anim : uint8_t\n            {\n";
                    src += animEnumerators(re->animations);
                    src += "            };\n";
                }
                src += "        };\n";
                src += "        " + base + "Item " + base + "_items["
                     + std::to_string(re->repeater.maxCount) + "]{};\n";
                // The item array is fixed-size, so _count is the compile-time capacity: read it
                // (e.g. to size a loop / divide a value), don't write it. A repeater that draws a
                // *variable* number of items uses a countVar (a canvas variable) instead.
                src += "        static constexpr uint32_t " + base + "_count = "
                     + std::to_string(re->repeater.maxCount) + ";\n";
            }

            // Animation channel offsets (Phase 5): one AnimState per animated element.
            std::vector<uint64_t> animUuids;
            collectAnimatedUuids(*c, animUuids);
            for (auto u : animUuids)
                src += "        P64::UI::AnimState " + animStateVar(u) + "{};\n";

            // Component-script state block (Phase 3): one flat blob per scripted element
            // + the current focus index. Reset/recreated per scene via UI::init().
            std::vector<ScriptedElem> se;
            for (const auto& e : c->elements) collectScriptedElements(e, codeIdx, se);
            if (!se.empty())
            {
                src += "        // UI component-script state (Phase 3)\n";
                src += "        P64::UI::UIElemState __elems[" + std::to_string(se.size()) + "]{};\n";
                src += "        int8_t __focusIndex{-1};\n";
            }

            // Typed clip + event enums, scoped to the canvas they belong to. The play/event
            // APIs take these (e.g. playAnimation<HUD>(HUDData::Anim::Bar), events.send(
            // HUDData::Event::Foo)); the enumerator value is the clip index / event wire type.
            std::vector<std::string> clipNames, evNames;
            for (const auto& a : c->animations) clipNames.push_back(a.name);
            for (const auto& e : c->events)     evNames.push_back(e.name);
            src += "\n        enum class Anim : uint8_t\n        {\n" + enumBody(clipNames) + "        };\n";
            src += "        enum class Event : uint16_t\n        {\n" + enumBody(evNames) + "        };\n";
            src += "        static constexpr uint16_t EventCount = "
                 + std::to_string(c->events.size()) + ";\n";

            src += "    };\n\n";
        }
        src += "}\n\n";

        src += "namespace P64::UI\n{\n";
        src += "    using namespace Types;\n\n";

        // Clip + event enums are scoped inside each <Name>Data (and per-item structs) above —
        // referenced here via UISceneTraits::Anim / ::Event.

        // UISceneType enum
        src += "    enum class UISceneType : uint8_t\n    {\n";
        for (const auto* c : canvases)
            src += "        " + c->name + ",\n";
        src += "        Count\n    };\n\n";

        // UISceneTraits primary template + specializations
        src += "    template<UISceneType Scene> struct UISceneTraits;\n\n";
        for (const auto* c : canvases)
        {
            src += "    template<> ";
            src += "    struct UISceneTraits<UISceneType::" + c->name + "> ";
            src += "    { using DataType = " + c->name + "Data; using Anim = " + c->name
                 + "Data::Anim; using Event = " + c->name + "Data::Event; };\n";
        }
        src += "\n";

        // GetSceneName
        src += "    constexpr const char* GetSceneName(UISceneType scene)\n    {\n";
        src += "        switch(scene)\n        {\n";
        for (const auto* c : canvases)
            src += "            case UISceneType::" + c->name + ": return \"" + c->name + "\";\n";
        src += "            default: return \"Unknown\";\n        }\n    }\n";

        src += "}\n";

        // Per-instance element-clip control (Phase 5e). Declarations so gameplay can drive a
        // clip on one repeated instance, e.g. P64::UI::Generated::Hud_Hearts_playItem(3, 0).
        {
            std::string decls;
            for (const auto* c : canvases)
            {
                std::vector<const CanvasElement*> ereps;
                for (const auto& e : c->elements) collectElemClipRepeaters(e, ereps);
                for (const auto* re : ereps)
                {
                    std::string pfx = c->name + "_" + repeaterBaseName(*re) + "_";
                    std::string at  = "P64::UI::Types::" + c->name + "Data::" + repeaterBaseName(*re) + "Item::Anim";
                    decls += "    void " + pfx + "playItem(uint32_t item, " + at + " clip, bool restart = true);\n";
                    decls += "    void " + pfx + "stopItem(uint32_t item, " + at + " clip);\n";
                    decls += "    void " + pfx + "seekItem(uint32_t item, " + at + " clip, float seconds);\n";
                }
            }
            if (!decls.empty())
                src += "\nnamespace P64::UI::Generated\n{\n" + decls + "}\n";
        }

        Utils::FS::saveTextFile(outPath, src);
    }

    void generateUIDriver(const std::vector<const Canvas*>& canvases,
                          const std::string& templatePath,
                          const std::string& outPath)
    {
        auto src = Utils::FS::loadTextFile(templatePath);

        // Build init entries
        std::string initEntries;
        for (const auto* c : canvases)
        {
            initEntries += "        detail::scenes[static_cast<size_t>(UISceneType::" + c->name
                        + ")] = new detail::UIModelView<UISceneType::" + c->name + ">();\n";
        }

        // Build template instantiations
        std::string instantiations;
        for (const auto* c : canvases)
        {
            auto& n = c->name;
            auto base = std::string{"P64::UI"};
            auto scType = base + "::UISceneType::" + n;
            auto modCb  = base + "::ModelCallback<" + scType + ">";
            auto viewCb = base + "::ViewCallback<" + scType + ">";
            auto dataT  = base + "::Types::" + n + "Data";
            auto viewFn = "void(*)(" + dataT + "&)";

            auto behFn  = base + "::BehaviorFn<" + scType + ">";
            auto animFn = base + "::AnimFn<" + scType + ">";

            instantiations += "template bool " + base + "::bindModel<" + scType + ">(" + modCb + ");\n";
            instantiations += "template bool " + base + "::bindView<" + scType  + ">(" + viewCb + ");\n";
            instantiations += "template bool " + base + "::bindViewFn<" + scType + ">(" + viewFn + ");\n";
            instantiations += "template bool " + base + "::bindBehaviorFn<" + scType + ">(" + behFn + ", uint8_t);\n";
            instantiations += "template bool " + base + "::bindAnimFn<" + scType + ">(" + animFn + ", uint8_t, uint16_t);\n";
            instantiations += "template void " + base + "::unbindModel<" + scType + ">();\n";
            instantiations += "template void " + base + "::unbindView<" + scType  + ">();\n";
            instantiations += "template void " + base + "::unbindViewFn<" + scType + ">();\n";
            instantiations += "template void " + base + "::unbindBehaviorFn<" + scType + ">();\n";
            instantiations += "template void " + base + "::unbindAnimFn<" + scType + ">();\n";
            auto animE = dataT + "::Anim";   // typed clip enum, nested in <Name>Data
            instantiations += "template void " + base + "::playAnimation<" + scType + ">(" + animE + ", bool);\n";
            instantiations += "template void " + base + "::stopAnimation<" + scType + ">(" + animE + ");\n";
            instantiations += "template void " + base + "::seekAnimation<" + scType + ">(" + animE + ", float);\n";
            instantiations += "template " + dataT + "* " + base + "::getViewData<" + scType + ">();\n";
        }

        src = Utils::replaceAll(src, "__UI_INIT_ENTRIES__", initEntries);
        src = Utils::replaceAll(src, "__CANVAS_TEMPLATE_INSTANTIATIONS__", instantiations);

        Utils::FS::saveTextFile(outPath, src);
    }

    // Emit the per-canvas component-script/focus "behavior" function (Phase 3).
    void generateBehaviorFn(std::string& src, const Canvas& c,
                            const std::vector<ScriptedElem>& se)
    {
        auto n = se.size();
        src += "    static void behavior_" + c.name + "(P64::UI::Types::" + c.name
             + "Data& data, uint16_t ownerId, P64::UI::BehaviorPhase phase, float dt,"
               " const P64::ObjectEvent* ev)\n    {\n";
        src += "        P64::Object* host = P64::ObjectRef{ownerId}.get();\n";
        src += "        if (!host) return;\n";

        // element -> script index + focusable tables
        src += "        static const uint32_t elemScript[" + std::to_string(n) + "] = {";
        for (size_t i = 0; i < n; ++i) src += (i ? ", " : " ") + std::to_string(se[i].scriptIdx);
        src += " };\n";
        src += "        static const bool elemFocus[" + std::to_string(n) + "] = {";
        for (size_t i = 0; i < n; ++i) src += std::string(i ? ", " : " ") + (se[i].focusable ? "true" : "false");
        src += " };\n";
        src += "        constexpr int N = " + std::to_string(n) + ";\n";
        src += "        using P64::Script::getCodeByIndex;\n\n";

        src += "        switch (phase)\n        {\n";
        src += "            case P64::UI::BehaviorPhase::Init:\n";
        src += "                for (int i=0;i<N;++i){ auto& s=getCodeByIndex(elemScript[i]);\n";
        src += "                    assertf(P64::Script::getCodeSizeByIndex(elemScript[i]) <= sizeof(P64::UI::UIElemState),\n";
        src += "                        \"UI script data (%u) exceeds UIElemState blob\", (unsigned)P64::Script::getCodeSizeByIndex(elemScript[i]));\n";
        src += "                    if (s.init) s.init(*host, &data.__elems[i]); }\n";
        src += "                break;\n";
        src += "            case P64::UI::BehaviorPhase::Destroy:\n";
        src += "                for (int i=0;i<N;++i){ auto& s=getCodeByIndex(elemScript[i]); if(s.destroy) s.destroy(*host,&data.__elems[i]); }\n";
        src += "                break;\n";
        src += "            case P64::UI::BehaviorPhase::Update:\n";
        src += "                for (int i=0;i<N;++i){ auto& s=getCodeByIndex(elemScript[i]); if(s.update) s.update(*host,&data.__elems[i],dt); }\n";
        src += "                break;\n";
        src += "            case P64::UI::BehaviorPhase::Event:\n";
        src += "                if (ev) for (int i=0;i<N;++i){ auto& s=getCodeByIndex(elemScript[i]); if(s.onEvent) s.onEvent(*host,&data.__elems[i],*ev); }\n";
        src += "                break;\n";
        src += "            case P64::UI::BehaviorPhase::FocusInput:\n";
        src += "            {\n";
        src += "                joypad_buttons_t btn = joypad_get_buttons_pressed(JOYPAD_PORT_1);\n";
        src += "                int cur = data.__focusIndex;\n";
        src += "                if (cur < 0) {\n";
        src += "                    for (int i=0;i<N;++i) if (elemFocus[i]) { cur=i; data.__focusIndex=(int8_t)i;\n";
        src += "                        auto& s=getCodeByIndex(elemScript[i]); if(s.onFocus) s.onFocus(*host,&data.__elems[i]); break; }\n";
        src += "                }\n";
        src += "                if (cur >= 0) {\n";
        src += "                    int dir = btn.d_down ? 1 : (btn.d_up ? -1 : 0);\n";
        src += "                    if (dir != 0) {\n";
        src += "                        int nx = cur; for (int s2=0;s2<N;++s2){ nx=(nx+dir+N)%N; if(elemFocus[nx]) break; }\n";
        src += "                        if (nx != cur) {\n";
        src += "                            auto& so=getCodeByIndex(elemScript[cur]); if(so.onBlur) so.onBlur(*host,&data.__elems[cur]);\n";
        src += "                            auto& sn=getCodeByIndex(elemScript[nx]);  if(sn.onFocus) sn.onFocus(*host,&data.__elems[nx]);\n";
        src += "                            data.__focusIndex=(int8_t)nx; cur=nx;\n";
        src += "                        }\n";
        src += "                    }\n";
        src += "                    if (btn.a) { auto& s=getCodeByIndex(elemScript[cur]); if(s.onActivate) s.onActivate(*host,&data.__elems[cur]); }\n";
        src += "                }\n";
        src += "                break;\n";
        src += "            }\n";
        src += "        }\n";
        src += "    }\n\n";
    }

    // True if a track channel is evaluated by the runtime tick.
    // All channels are now evaluated; Color uses a separate 4-component path.
    bool channelEvaluated(CanvasTrackChannel ch)
    {
        return ch == CanvasTrackChannel::X || ch == CanvasTrackChannel::Y
            || ch == CanvasTrackChannel::Rotation || ch == CanvasTrackChannel::Scale
            || ch == CanvasTrackChannel::Variable || ch == CanvasTrackChannel::FrameIndex
            || ch == CanvasTrackChannel::Alpha || ch == CanvasTrackChannel::Color;
    }

    // Emit one keyframe table: scalar AnimKey, or AnimKeyColor for the Color channel.
    void emitTrackTable(std::string& src, const std::string& tbl, const CanvasTimelineTrack& t)
    {
        if (t.channel == CanvasTrackChannel::Color)
        {
            src += "    static const AnimKeyColor " + tbl + "[] = {";
            for (size_t ki = 0; ki < t.keys.size(); ++ki)
            {
                const auto& k = t.keys[ki];
                float c4[4] = {255, 255, 255, 255};
                if (k.value.is_array())
                    for (int i = 0; i < 4 && i < (int)k.value.size(); ++i)
                        if (k.value[i].is_number()) c4[i] = k.value[i].get<float>();
                src += std::string(ki ? ", " : " ") + "{" + floatLit(k.time) + ", {"
                     + floatLit(c4[0]) + ", " + floatLit(c4[1]) + ", " + floatLit(c4[2]) + ", "
                     + floatLit(c4[3]) + "}, " + std::to_string((int)k.easing) + "}";
            }
            src += " };\n";
            return;
        }
        src += "    static const AnimKey " + tbl + "[] = {";
        for (size_t ki = 0; ki < t.keys.size(); ++ki)
        {
            const auto& k = t.keys[ki];
            float v = k.value.is_number() ? k.value.get<float>() : 0.0f;
            src += std::string(ki ? ", " : " ") + "{" + floatLit(k.time) + ", "
                 + floatLit(v) + ", " + std::to_string((int)k.easing) + "}";
        }
        src += " };\n";
    }

    // Emit a single track's per-frame write into an AnimState container, evaluated at `c.elapsed`.
    // `state` is the prefix before the AnimState member: "data." (canvas/flat) or
    // "data.<base>_items[i]." (per-instance). Variable channels always write the canvas variable.
    void emitTrackWrite(std::string& src, const CanvasTimelineTrack& t, const std::string& tbl,
                        const std::string& state, const std::string& indent)
    {
        std::string val = "evalTrack(" + tbl + ", " + std::to_string(t.keys.size()) + ", c.elapsed)";
        if (t.channel == CanvasTrackChannel::Variable) {
            // state = "data." (canvas var) or "data.<base>_items[i]." (per-instance item field).
            if (!t.varName.empty())
                src += indent + state + t.varName + " = " + val + ";\n";
        } else if (t.channel == CanvasTrackChannel::FrameIndex) {
            src += indent + state + animStateVar(t.targetUuid) + ".frameIndex = (int32_t)(" + val + ");\n";
        } else if (t.channel == CanvasTrackChannel::Alpha) {
            // Clamp to [0,1] so prim-alpha never wraps; matches the editor preview.
            src += indent + "{ float __a = " + val + "; " + state + animStateVar(t.targetUuid)
                 + ".alpha = __a < 0.f ? 0.f : (__a > 1.f ? 1.f : __a); }\n";
        } else if (t.channel == CanvasTrackChannel::Color) {
            // hasColor latches on by design (a finished clip holds its last color, like dx/scale).
            // Use __ci (not i) for the component loop — per-instance `state` contains items[i],
            // so a plain `i` would shadow the repeater instance index.
            std::string av = state + animStateVar(t.targetUuid);
            src += indent + "{ uint8_t __c[4]; evalTrackColor(" + tbl + ", "
                 + std::to_string(t.keys.size()) + ", c.elapsed, __c);\n";
            src += indent + "  for (int __ci = 0; __ci < 4; ++__ci) " + av + ".color[__ci] = __c[__ci];\n";
            src += indent + "  " + av + ".hasColor = 1; }\n";
        } else {
            src += indent + state + animStateVar(t.targetUuid) + "."
                 + channelField(t.channel) + " = " + val + ";\n";
        }
    }

    // Emit a canvas's keyframe tables + tickAnims_<Name> (Phase 5 / 5e).
    void generateAnimTick(std::string& src, const Canvas& c)
    {
        const auto& anims = c.animations;

        // Repeaters owning element-scoped clips (per-instance animation, Phase 5e).
        std::vector<const CanvasElement*> ereps;
        for (const auto& e : c.elements) collectElemClipRepeaters(e, ereps);

        // Static keyframe tables: canvas clips (k_*) ...
        for (size_t ai = 0; ai < anims.size(); ++ai)
            for (size_t ti = 0; ti < anims[ai].tracks.size(); ++ti)
            {
                const auto& t = anims[ai].tracks[ti];
                if (!channelEvaluated(t.channel) || t.keys.empty()) continue;
                emitTrackTable(src, "k_" + c.name + "_" + std::to_string(ai) + "_" + std::to_string(ti), t);
            }
        // ... and element clips (ek_<repIdx>_*).
        for (size_t ri = 0; ri < ereps.size(); ++ri)
            for (size_t ai = 0; ai < ereps[ri]->animations.size(); ++ai)
                for (size_t ti = 0; ti < ereps[ri]->animations[ai].tracks.size(); ++ti)
                {
                    const auto& t = ereps[ri]->animations[ai].tracks[ti];
                    // Variable channels in element clips write a per-instance item field (Part A).
                    if (!channelEvaluated(t.channel) || t.keys.empty()) continue;
                    emitTrackTable(src, "ek_" + c.name + "_" + std::to_string(ri) + "_"
                                 + std::to_string(ai) + "_" + std::to_string(ti), t);
                }

        src += "    static void tickAnims_" + c.name + "(P64::UI::Types::" + c.name
             + "Data& data, P64::UI::AnimPlayback* clips, P64::UI::EventSink& events,"
               " uint16_t ownerId, float dt)\n    {\n";
        src += "        (void)data; (void)events; (void)ownerId;\n";
        for (size_t ai = 0; ai < anims.size(); ++ai)
        {
            const auto& a = anims[ai];
            std::string ci = std::to_string(ai);
            // Driven (Part B): time = clamp(<timeVar>, 0, dur) every frame; else playback advance.
            bool driven = (a.timeSource == 1 && !a.timeVar.empty());
            src += "        { auto& c = clips[" + ci + "];\n";
            src += "          const float dur = " + floatLit(a.duration) + ";\n";
            if (driven) {
                src += "          { c.prevElapsed = c.elapsed;\n";
                src += "            float __ts = (float)data." + a.timeVar + ";\n";
                src += "            c.elapsed = __ts < 0.f ? 0.f : (__ts > dur ? dur : __ts);\n";
            } else {
                src += "          if (c.playing) {\n";
                src += "            c.prevElapsed = c.elapsed; c.elapsed += dt;\n";
                src += "            if (c.elapsed >= dur) { c.elapsed = dur; c.playing = 0; c.finished = 1; }\n";
            }

            for (size_t ti = 0; ti < a.tracks.size(); ++ti)
            {
                const auto& t = a.tracks[ti];
                if (!channelEvaluated(t.channel) || t.keys.empty()) continue;
                emitTrackWrite(src, t, "k_" + c.name + "_" + ci + "_" + std::to_string(ti),
                               "data.", "            ");
            }

            for (const auto& ev : a.events)
            {
                src += "            if (" + floatLit(ev.time) + " > c.prevElapsed && "
                     + floatLit(ev.time) + " <= c.elapsed) {\n";
                if (ev.action == CanvasTimelineEvent::Action::FireEvent) {
                    int idx = eventIndex(c, ev.eventName);
                    if (idx >= 0)
                        src += "                events.send(" + std::to_string(idx) + ");\n";
                } else { // Seek (applied inline so loops are seamless)
                    int target = ev.targetAnim < 0 ? (int)ai : ev.targetAnim;
                    if (target >= 0 && target < (int)anims.size()) {
                        std::string tc = "clips[" + std::to_string(target) + "]";
                        src += "                " + tc + ".elapsed = " + tc + ".prevElapsed = "
                             + floatLit(ev.seekTime) + ";\n";
                        src += "                " + tc + ".playing = 1; " + tc + ".finished = 0;\n";
                    } // else: out-of-range Seek target — skipped at codegen (no OOB clips[] write)
                }
                src += "            }\n";
            }
            src += "          }\n        }\n";
        }

        // Element-scoped clips (Phase 5e): each repeated instance ticks its own playback +
        // AnimState (in items[i]). Code-triggered only (playItem) — no autoPlay per instance.
        for (size_t ri = 0; ri < ereps.size(); ++ri)
        {
            const auto* re = ereps[ri];
            auto base = repeaterBaseName(*re);
            std::string items = "data." + base + "_items[i]";
            src += "        for (uint32_t i = 0; i < (uint32_t)" + repeaterCountExpr(*re)
                 + " && i < " + std::to_string(re->repeater.maxCount) + "u; ++i) {\n";
            for (size_t ai = 0; ai < re->animations.size(); ++ai)
            {
                const auto& a = re->animations[ai];
                std::string ek = "ek_" + c.name + "_" + std::to_string(ri) + "_" + std::to_string(ai) + "_";
                // Driven (Part B): per-instance time from a float item field (or canvas var).
                bool driven = (a.timeSource == 1 && !a.timeVar.empty());
                std::string tsrc;
                if (driven) {
                    bool isField = false;
                    for (const auto& f : re->repeater.itemFields) if (f.name == a.timeVar) { isField = true; break; }
                    tsrc = isField ? (items + "." + a.timeVar) : ("data." + a.timeVar);
                }
                src += "          { auto& c = " + items + ".__pb[" + std::to_string(ai) + "];\n";
                src += "            const float dur = " + floatLit(a.duration) + ";\n";
                if (driven) {
                    src += "            { c.prevElapsed = c.elapsed;\n";
                    src += "              float __ts = (float)" + tsrc + ";\n";
                    src += "              c.elapsed = __ts < 0.f ? 0.f : (__ts > dur ? dur : __ts);\n";
                } else {
                    src += "            if (c.playing) {\n";
                    src += "              c.prevElapsed = c.elapsed; c.elapsed += dt;\n";
                    src += "              if (c.elapsed >= dur) { c.elapsed = dur; c.playing = 0; c.finished = 1; }\n";
                }
                for (size_t ti = 0; ti < a.tracks.size(); ++ti)
                {
                    const auto& t = a.tracks[ti];
                    if (!channelEvaluated(t.channel) || t.keys.empty()) continue;
                    emitTrackWrite(src, t, ek + std::to_string(ti), items + ".", "              ");
                }
                for (const auto& ev : a.events)
                {
                    src += "              if (" + floatLit(ev.time) + " > c.prevElapsed && "
                         + floatLit(ev.time) + " <= c.elapsed) {\n";
                    if (ev.action == CanvasTimelineEvent::Action::FireEvent) {
                        int idx = eventIndex(c, ev.eventName);
                        if (idx >= 0)
                            src += "                events.send(" + std::to_string(idx) + ");\n";
                    } else { // Seek targets this instance's playback (per-instance loop)
                        int target = ev.targetAnim < 0 ? (int)ai : ev.targetAnim;
                        if (target >= 0 && target < (int)re->animations.size()) {
                            std::string tc = items + ".__pb[" + std::to_string(target) + "]";
                            src += "                " + tc + ".elapsed = " + tc + ".prevElapsed = "
                                 + floatLit(ev.seekTime) + ";\n";
                            src += "                " + tc + ".playing = 1; " + tc + ".finished = 0;\n";
                        } // else: out-of-range Seek target — skipped at codegen
                    }
                    src += "              }\n";
                }
                src += "            }\n          }\n";
            }
            src += "        }\n";
        }
        src += "    }\n\n";
    }

    void generateUIBindings(const std::vector<const Canvas*>& canvases,
                            const std::unordered_map<uint64_t, uint32_t>& codeIdx,
                            AssetManager& assets,
                            const std::string& outPath)
    {
        std::string src;
        src += "// NOTE: Auto-Generated File!\n";
        src += "#include \"ui/UI.h\"\n";
        src += "#include \"assetTable.h\"\n";   // needed for _asset literal and baked sprite refs
        src += "#include <libdragon.h>\n";
        src += "#include <rdpq.h>\n";
        src += "#include <rdpq_sprite.h>\n";
        src += "#include <rdpq_tex.h>\n";   // rdpq_texparms_t + REPEAT_INFINITE (wrap/frame)
        src += "#include <rdpq_text.h>\n";
        src += "#include \"script/scriptTable.h\"\n";  // component-script dispatch
        src += "#include \"ui/uiEval.h\"\n";            // ease/lerp for timeline interpolation
        src += "#include \"renderer/drawLayer.h\"\n\n";

        bool anyAnims = false, anyColorAnims = false;
        auto scanClip = [&](const CanvasAnimation& a) {
            anyAnims = true;
            for (const auto& t : a.tracks)
                if (t.channel == CanvasTrackChannel::Color) anyColorAnims = true;
        };
        for (const auto* c : canvases) {
            for (const auto& a : c->animations) scanClip(a);
            std::vector<const CanvasElement*> ereps;          // element clips also need evaluators
            for (const auto& e : c->elements) collectElemClipRepeaters(e, ereps);
            for (const auto* re : ereps) for (const auto& a : re->animations) scanClip(a);
        }

        src += "namespace P64::UI::Generated\n{\n";

        // Shared timeline keyframe type + evaluator (Phase 5).
        if (anyAnims)
        {
            src += "    struct AnimKey { float t; float v; uint8_t e; };\n";
            src += "    static float evalTrack(const AnimKey* k, int n, float et)\n    {\n";
            src += "        if (n <= 0) return 0.0f;\n";
            src += "        if (et <= k[0].t) return k[0].v;\n";
            src += "        if (et >= k[n-1].t) return k[n-1].v;\n";
            src += "        int i = 0; while (i+1 < n && k[i+1].t <= et) ++i;\n";
            src += "        float lt = (k[i+1].t > k[i].t) ? (et - k[i].t) / (k[i+1].t - k[i].t) : 0.0f;\n";
            src += "        return P64::UI::Eval::lerp(k[i].v, k[i+1].v, P64::UI::Eval::applyEase((int)k[i].e, lt));\n";
            src += "    }\n\n";
        }
        // 4-component [r,g,b,a] evaluator for animated Color channels (Phase 5d).
        if (anyColorAnims)
        {
            src += "    struct AnimKeyColor { float t; float v[4]; uint8_t e; };\n";
            src += "    static void evalTrackColor(const AnimKeyColor* k, int n, float et, uint8_t out[4])\n    {\n";
            src += "        if (n <= 0) { out[0]=out[1]=out[2]=out[3]=255; return; }\n";
            src += "        if (et <= k[0].t)   { for (int i=0;i<4;++i) out[i]=(uint8_t)k[0].v[i];   return; }\n";
            src += "        if (et >= k[n-1].t) { for (int i=0;i<4;++i) out[i]=(uint8_t)k[n-1].v[i]; return; }\n";
            src += "        int i = 0; while (i+1 < n && k[i+1].t <= et) ++i;\n";
            src += "        float lt = (k[i+1].t > k[i].t) ? (et - k[i].t) / (k[i+1].t - k[i].t) : 0.0f;\n";
            src += "        float ez = P64::UI::Eval::applyEase((int)k[i].e, lt);\n";
            src += "        for (int c = 0; c < 4; ++c) out[c] = (uint8_t)P64::UI::Eval::lerp(k[i].v[c], k[i+1].v[c], ez);\n";
            src += "    }\n\n";
        }

        for (const auto* c : canvases)
        {
            // Non-const: AssetRef::get() is non-const (lazy-loads on first call)
            auto dataType = "P64::UI::Types::" + c->name + "Data&";
            src += "    static void draw_" + c->name + "_fn(" + dataType + " data)\n    {\n";

            // Static AssetRefs for baked (non-variable) sprite properties
            for (const auto& e : c->elements)
                emitBakedSprites(src, e, assets);

            src += "\n        P64::DrawLayer::use2D();\n\n";

            // Mark which elements are animated so the draw code composites offsets.
            g_animatedUuids.clear();
            collectAnimatedUuids(*c, g_animatedUuids);
            g_frameAnimatedUuids.clear();
            collectFrameAnimatedUuids(*c, g_frameAnimatedUuids);
            g_alphaAnimatedUuids.clear();
            g_colorAnimatedUuids.clear();
            collectTintAnimatedUuids(*c, g_alphaAnimatedUuids, g_colorAnimatedUuids);
            // Phase 5e: element-scoped clips also drive alpha/color/frame (per-instance). The
            // draw reads these via the item-local g_animRef set inside the repeater loop.
            for (const auto& e : c->elements)
                collectElemClipChannelUuids(e, g_alphaAnimatedUuids, g_colorAnimatedUuids, g_frameAnimatedUuids);

            for (const auto& e : c->elements)
                generateElementCode(src, e);

            g_animatedUuids.clear();
            g_frameAnimatedUuids.clear();
            g_alphaAnimatedUuids.clear();
            g_colorAnimatedUuids.clear();

            src += "\n        P64::DrawLayer::useDefault();\n";
            src += "    }\n\n";
        }

        // Component-script/focus behavior functions (only for canvases with scripts).
        for (const auto* c : canvases)
        {
            std::vector<ScriptedElem> se;
            for (const auto& e : c->elements) collectScriptedElements(e, codeIdx, se);
            if (!se.empty())
                generateBehaviorFn(src, *c, se);
        }

        // Animation-tick functions (only for canvases with clips OR element clips).
        for (const auto* c : canvases)
        {
            bool anyElemClip = false;
            std::vector<const CanvasElement*> ereps;
            for (const auto& e : c->elements) collectElemClipRepeaters(e, ereps);
            anyElemClip = !ereps.empty();
            if (!c->animations.empty() || anyElemClip)
                generateAnimTick(src, *c);
        }

        // Per-instance element-clip control functions (Phase 5e). Reach the live view data via
        // getViewData<Scene>() and toggle items[i].__pb[clip] playback.
        for (const auto* c : canvases)
        {
            std::vector<const CanvasElement*> ereps;
            for (const auto& e : c->elements) collectElemClipRepeaters(e, ereps);
            for (const auto* re : ereps)
            {
                auto base   = repeaterBaseName(*re);
                std::string pfx   = c->name + "_" + base + "_";
                std::string scene = "P64::UI::UISceneType::" + c->name;
                std::string at    = "P64::UI::Types::" + c->name + "Data::" + base + "Item::Anim";
                std::string items = base + "_items";
                std::string maxc  = std::to_string(re->repeater.maxCount);
                // Cast the typed clip enum to its index, then bound-check item + clip.
                std::string head  = "        auto* d = P64::UI::getViewData<" + scene + ">();\n"
                                    "        uint8_t c = (uint8_t)clip;\n"
                                    "        if (!d || item >= " + maxc + "u || c >= "
                                    + std::to_string(re->animations.size()) + ") return;\n";
                src += "    void " + pfx + "playItem(uint32_t item, " + at + " clip, bool restart)\n    {\n"
                     + head
                     + "        auto& pb = d->" + items + "[item].__pb[c];\n"
                     + "        if (restart) { pb.elapsed = 0.f; pb.prevElapsed = 0.f; }\n"
                     + "        pb.playing = 1; pb.finished = 0;\n    }\n";
                src += "    void " + pfx + "stopItem(uint32_t item, " + at + " clip)\n    {\n"
                     + head
                     + "        d->" + items + "[item].__pb[c].playing = 0;\n    }\n";
                src += "    void " + pfx + "seekItem(uint32_t item, " + at + " clip, float seconds)\n    {\n"
                     + head
                     + "        auto& pb = d->" + items + "[item].__pb[c]; pb.elapsed = pb.prevElapsed = seconds;\n    }\n";
            }
        }

        // uiInitializeBindings — binds the stateless view fn (+ behavior fn if scripted).
        src += "    void uiInitializeBindings()\n    {\n";
        for (const auto* c : canvases)
        {
            src += "        P64::UI::bindViewFn<P64::UI::UISceneType::"
                 + c->name + ">(draw_" + c->name + "_fn);\n";

            std::vector<ScriptedElem> se;
            for (const auto& e : c->elements) collectScriptedElements(e, codeIdx, se);
            if (!se.empty())
            {
                size_t focusCount = 0;
                for (const auto& s : se) if (s.focusable) ++focusCount;
                src += "        P64::UI::bindBehaviorFn<P64::UI::UISceneType::"
                     + c->name + ">(behavior_" + c->name + ", " + std::to_string(focusCount) + ");\n";
            }

            std::vector<const CanvasElement*> ereps;
            for (const auto& e : c->elements) collectElemClipRepeaters(e, ereps);
            if (!c->animations.empty() || !ereps.empty())
            {
                uint32_t autoMask = 0;
                for (size_t ai = 0; ai < c->animations.size() && ai < 16; ++ai)
                    if (c->animations[ai].autoPlay) autoMask |= (1u << ai);
                src += "        P64::UI::bindAnimFn<P64::UI::UISceneType::"
                     + c->name + ">(tickAnims_" + c->name + ", "
                     + std::to_string(c->animations.size()) + ", " + std::to_string(autoMask) + ");\n";
            }
        }
        src += "    }\n";
        src += "}\n";

        Utils::FS::saveTextFile(outPath, src);
    }
}

// =====================================================================
// Public entry point
// =====================================================================

bool Build::buildCanvasAssets(::Project::Project& project, SceneCtx& sceneCtx)
{
    auto canvasAssets = project.getAssets().getTypeEntries(FileType::CANVAS);
    if (canvasAssets.empty())
    {
        sceneCtx.hasCanvases = false;
        return true;
    }

    sceneCtx.hasCanvases = true;

    std::vector<const Canvas*> canvases;
    for (const auto& entry : canvasAssets)
    {
        if (entry.canvas)
            canvases.push_back(entry.canvas.get());
    }

    if (canvases.empty()) return true;

    auto p64SrcPath = fs::path{project.getPath()} / "src" / "p64";
    if (!fs::exists(p64SrcPath))
        fs::create_directories(p64SrcPath);

    auto typesOut   = (p64SrcPath / "UISceneTypes.h").string();
    auto driverOut  = (p64SrcPath / "uiDriver.cpp").string();
    auto bindingsOut = (p64SrcPath / "uiBindings.cpp").string();

    Utils::Logger::log("Building canvas assets...");
    generateUISceneTypes(canvases, sceneCtx.codeIdxMapUUID, typesOut);
    generateUIDriver(canvases, "data/scripts/uiDriver.cpp", driverOut);
    generateUIBindings(canvases, sceneCtx.codeIdxMapUUID, project.getAssets(), bindingsOut);

    return true;
}
