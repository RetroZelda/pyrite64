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

namespace fs = std::filesystem;
using namespace Project;

namespace
{
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
        if (p.isBound && !p.boundVar.empty())
            return dataRef + p.boundVar;

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
                        const std::string& fallback = "")
    {
        auto it = e.props.find(key);
        if (it == e.props.end()) return fallback;
        return propExpr(it->second, fallback);
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
                          Project::AssetManager& assets)
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
            out += "    static P64::AssetRef<sprite_t> " + bakedSpriteVarName(e)
                 + "{(sprite_t*)(uintptr_t)(\"" + romPath + "\"_asset)};\n";
        }
        for (const auto& child : e.children)
            emitBakedSprites(out, child, assets);
    }

    // =====================================================================
    // Per-element draw code generators
    // =====================================================================

    void generateSpriteCode(std::string& out, const CanvasElement& e)
    {
        auto x       = posIntExpr(e.x, 0);
        auto y       = posIntExpr(e.y, 0);
        auto scaleX  = getPropFloat(e, "scaleX",  1.0f);
        auto scaleY  = getPropFloat(e, "scaleY",  1.0f);
        auto blend   = getProp(e, "blendMode",  "0");
        auto alpha   = getPropInt(e,  "alphaCompare", 0);
        auto sprite  = spriteExpr(e, "sprite");

        // Combiner is a C macro name — emit unquoted (ident=true)
        bool hasCombiner = e.props.count("combiner");
        std::string combiner = "RDPQ_COMBINER_TEX";
        if (hasCombiner) {
            auto it = e.props.find("combiner");
            combiner = propExpr(it->second, "RDPQ_COMBINER_TEX", "data.", /*ident=*/true);
        }

        bool hasColor = e.props.count("color");
        std::string color = hasColor ? getProp(e, "color", "{0xFF,0xFF,0xFF,0xFF}")
                                     : "";

        out += "    // " + e.name + " [Sprite]\n";
        out += "    if (" + sprite + ") {\n";
        out += "        rdpq_mode_begin();\n";
        out += "          rdpq_mode_blender(" + blend + ");\n";
        if (!alpha.empty() && alpha != "0")
            out += "          rdpq_mode_alphacompare(" + alpha + ");\n";
        out += "          rdpq_mode_combiner(" + combiner + ");\n";
        out += "        rdpq_mode_end();\n";
        if (hasColor && !color.empty())
            out += "        rdpq_set_prim_color(" + color + ");\n";
        out += "        rdpq_blitparms_t __p_" + std::to_string(e.uuid & 0xFFFF) + "{};\n";
        out += "        __p_" + std::to_string(e.uuid & 0xFFFF) + ".scale_x = " + scaleX + ";\n";
        out += "        __p_" + std::to_string(e.uuid & 0xFFFF) + ".scale_y = " + scaleY + ";\n";
        out += "        rdpq_sprite_blit(" + sprite + ", " + x + ", " + y + ", "
             + "&__p_" + std::to_string(e.uuid & 0xFFFF) + ");\n";
        out += "    }\n";
    }

    void generateTextCode(std::string& out, const CanvasElement& e)
    {
        auto x     = posIntExpr(e.x, 0);
        auto y     = posIntExpr(e.y, 0);
        auto scale = getPropFloat(e, "scale", 1.0f);
        auto style = getPropInt(e, "style", 0);
        bool hasColor = e.props.count("color");
        std::string color = hasColor ? getProp(e, "color", "{0xFF,0xFF,0xFF,0xFF}") : "";

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
        out += "    // " + e.name + " [Text]\n";
        out += "    {\n";
        if (hasColor && !color.empty())
            out += "        rdpq_set_prim_color(" + color + ");\n";
        out += "        rdpq_textparms_t __tp_" + id + "{};\n";
        out += "        __tp_" + id + ".style_id = " + style + ";\n";
        out += "        rdpq_text_printf(&__tp_" + id
             + ", " + fontIdExpr + ", " + x + ", " + y + ", " + textExpr;
        for (const auto& arg : argExprs)
            out += ", " + arg;
        out += ");\n";
        out += "    }\n";
    }

    void generateColorRectCode(std::string& out, const CanvasElement& e)
    {
        auto x      = posIntExpr(e.x, 0);
        auto y      = posIntExpr(e.y, 0);
        auto w      = getPropFloat(e, "width",  32.0f);
        auto h      = getPropFloat(e, "height", 32.0f);
        bool hasColor = e.props.count("color");
        std::string color = hasColor ? getProp(e, "color", "{0xFF,0xFF,0xFF,0xFF}") : "{0xFF,0xFF,0xFF,0xFF}";

        out += "    // " + e.name + " [ColorRect]\n";
        out += "    rdpq_set_prim_color(" + color + ");\n";
        out += "    rdpq_mode_begin();\n";
        out += "      rdpq_mode_combiner(RDPQ_COMBINER_FLAT);\n";
        out += "    rdpq_mode_end();\n";
        out += "    rdpq_fill_rectangle(" + x + ", " + y
             + ", (" + x + ") + (" + w + "), (" + y + ") + (" + h + "));\n";
    }

    void generateTextureRectCode(std::string& out, const CanvasElement& e)
    {
        auto x   = posIntExpr(e.x, 0);
        auto y   = posIntExpr(e.y, 0);
        auto w   = getPropIntExpr(e, "width",  32);
        auto h   = getPropIntExpr(e, "height", 32);
        auto s0  = getPropIntExpr(e, "s0", 0);
        auto t0  = getPropIntExpr(e, "t0", 0);
        auto sprite = spriteExpr(e, "sprite");
        bool hasColor = e.props.count("color");
        std::string color = hasColor ? getProp(e, "color", "{0xFF,0xFF,0xFF,0xFF}") : "";

        out += "    // " + e.name + " [TextureRect]\n";
        out += "    if (" + sprite + ") {\n";
        out += "        rdpq_mode_begin();\n";
        out += "          rdpq_mode_combiner(RDPQ_COMBINER_TEX);\n";
        out += "        rdpq_mode_end();\n";
        if (hasColor && !color.empty())
            out += "        rdpq_set_prim_color(" + color + ");\n";
        out += "        rdpq_sprite_upload(TILE0, " + sprite + ", nullptr);\n";
        out += "        rdpq_texture_rectangle(TILE0, " + x + ", " + y + ", "
             + x + " + " + w + ", " + y + " + " + h + ", "
             + s0 + ", " + t0 + ");\n";
        out += "    }\n";
    }

    void generateElementCode(std::string& out, const CanvasElement& e)
    {
        if (!e.visible) return;

        switch (e.type)
        {
            case CanvasElementType::Sprite:      generateSpriteCode(out, e);      break;
            case CanvasElementType::Text:        generateTextCode(out, e);        break;
            case CanvasElementType::ColorRect:   generateColorRectCode(out, e);   break;
            case CanvasElementType::TextureRect: generateTextureRectCode(out, e); break;
        }

        for (const auto& child : e.children)
            generateElementCode(out, child);
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

    void generateUISceneTypes(const std::vector<const Canvas*>& canvases,
                              const std::string& outPath)
    {
        std::string src;
        src += "// NOTE: Auto-Generated File!\n";
        src += "#pragma once\n";
        src += "#include <cstdint>\n";
        src += "#include <assets/assetManager.h>\n";
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
            src += "    };\n\n";
        }
        src += "}\n\n";

        // Emit X-macro
        src += "namespace P64::UI\n{\n";
        src += "    using namespace Types;\n\n";
        src += "    #define UI_SCENE_TYPES \\\n";
        for (const auto* c : canvases)
            src += "        X(" + c->name + ", " + c->name + "Data) \\\n";
        src += "\n}\n\n";

        // Include base infrastructure
        src += "#include \"ui/UISceneTypes_base.h\"\n";

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

            instantiations += "template bool " + base + "::bindModel<" + scType + ">(" + modCb + ");\n";
            instantiations += "template bool " + base + "::bindView<" + scType  + ">(" + viewCb + ");\n";
            instantiations += "template bool " + base + "::bindViewFn<" + scType + ">(" + viewFn + ");\n";
            instantiations += "template void " + base + "::unbindModel<" + scType + ">();\n";
            instantiations += "template void " + base + "::unbindView<" + scType  + ">();\n";
            instantiations += "template void " + base + "::unbindViewFn<" + scType + ">();\n";
        }

        src = Utils::replaceAll(src, "__UI_INIT_ENTRIES__", initEntries);
        src = Utils::replaceAll(src, "__CANVAS_TEMPLATE_INSTANTIATIONS__", instantiations);

        Utils::FS::saveTextFile(outPath, src);
    }

    void generateUIBindings(const std::vector<const Canvas*>& canvases,
                            Project::AssetManager& assets,
                            const std::string& outPath)
    {
        std::string src;
        src += "// NOTE: Auto-Generated File!\n";
        src += "#include \"ui/UI.h\"\n";
        src += "#include \"assetTable.h\"\n";   // needed for _asset literal and baked sprite refs
        src += "#include <rdpq.h>\n";
        src += "#include <rdpq_sprite.h>\n";
        src += "#include <rdpq_text.h>\n";
        src += "#include \"renderer/drawLayer.h\"\n\n";

        src += "namespace P64::UI::Generated\n{\n";

        for (const auto* c : canvases)
        {
            // Non-const: AssetRef::get() is non-const (lazy-loads on first call)
            auto dataType = "P64::UI::Types::" + c->name + "Data&";
            src += "    static void draw_" + c->name + "_fn(" + dataType + " data)\n    {\n";

            // Static AssetRefs for baked (non-variable) sprite properties
            for (const auto& e : c->elements)
                emitBakedSprites(src, e, assets);

            src += "\n        P64::DrawLayer::use2D();\n\n";

            for (const auto& e : c->elements)
                generateElementCode(src, e);

            src += "\n        P64::DrawLayer::useDefault();\n";
            src += "    }\n\n";
        }

        // uiInitializeBindings — uses bindViewFn (no Object& needed)
        src += "    void uiInitializeBindings()\n    {\n";
        for (const auto* c : canvases)
        {
            src += "        P64::UI::bindViewFn<P64::UI::UISceneType::"
                 + c->name + ">(draw_" + c->name + "_fn);\n";
        }
        src += "    }\n";
        src += "}\n";

        Utils::FS::saveTextFile(outPath, src);
    }
}

// =====================================================================
// Public entry point
// =====================================================================

bool Build::buildCanvasAssets(Project::Project& project, SceneCtx& sceneCtx)
{
    auto canvasAssets = project.getAssets().getTypeEntries(Project::FileType::CANVAS);
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
    generateUISceneTypes(canvases, typesOut);
    generateUIDriver(canvases, "data/scripts/uiDriver.cpp", driverOut);
    generateUIBindings(canvases, project.getAssets(), bindingsOut);

    return true;
}
