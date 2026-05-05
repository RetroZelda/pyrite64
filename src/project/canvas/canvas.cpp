/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "canvas.h"
#include "../../utils/fs.h"
#include "../../utils/hash.h"

namespace Project
{
    // =====================================================================
    // CanvasVariableDef
    // =====================================================================

    std::string CanvasVariableDef::serialize() const
    {
        nlohmann::json j;
        j["name"] = name;
        j["type"] = static_cast<int>(type);
        j["default"] = defaultValue;
        return j.dump();
    }

    CanvasVariableDef CanvasVariableDef::deserialize(const nlohmann::json& j)
    {
        CanvasVariableDef v;
        v.name = j.value("name", "");
        v.type = static_cast<CanvasVarType>(j.value("type", 0));
        v.defaultValue = j.value("default", "");
        return v;
    }

    // =====================================================================
    // PropValue
    // =====================================================================

    std::string PropValue::serialize() const
    {
        nlohmann::json j;
        j["isBound"] = isBound;
        if (isBound)
            j["var"] = boundVar;
        else
            j["val"] = value;
        return j.dump();
    }

    PropValue PropValue::deserialize(const nlohmann::json& j)
    {
        PropValue p;
        p.isBound = j.value("isBound", false);
        if (p.isBound)
            p.boundVar = j.value("var", "");
        else
            p.value = j.contains("val") ? j["val"] : nlohmann::json{};
        return p;
    }

    // =====================================================================
    // CanvasElement
    // =====================================================================

    namespace
    {
        PropValue litFloat(float v)
        {
            PropValue p;
            p.value = v;
            return p;
        }
    }

    std::string CanvasElement::serialize() const
    {
        nlohmann::json j;
        j["name"] = name;
        j["type"] = static_cast<int>(type);
        j["uuid"] = uuid;
        j["x"] = nlohmann::json::parse(x.serialize());
        j["y"] = nlohmann::json::parse(y.serialize());
        j["scaleX"] = nlohmann::json::parse(scaleX.serialize());
        j["scaleY"] = nlohmann::json::parse(scaleY.serialize());
        j["rotation"] = nlohmann::json::parse(rotation.serialize());
        j["visible"] = visible;

        nlohmann::json propsJson = nlohmann::json::object();
        for (const auto& [k, v] : props)
            propsJson[k] = nlohmann::json::parse(v.serialize());
        j["props"] = propsJson;

        nlohmann::json childrenJson = nlohmann::json::array();
        for (const auto& child : children)
            childrenJson.push_back(nlohmann::json::parse(child.serialize()));
        j["children"] = childrenJson;

        return j.dump();
    }

    CanvasElement CanvasElement::deserialize(const nlohmann::json& j)
    {
        CanvasElement e;
        e.name = j.value("name", "");
        e.type = static_cast<CanvasElementType>(j.value("type", 0));
        e.uuid = j.value("uuid", uint64_t{0});
        e.visible = j.value("visible", true);

        if (j.contains("x")) e.x = PropValue::deserialize(j["x"]);
        else e.x = litFloat(0.f);

        if (j.contains("y")) e.y = PropValue::deserialize(j["y"]);
        else e.y = litFloat(0.f);

        if (j.contains("scaleX")) e.scaleX = PropValue::deserialize(j["scaleX"]);
        else e.scaleX = litFloat(1.f);

        if (j.contains("scaleY")) e.scaleY = PropValue::deserialize(j["scaleY"]);
        else e.scaleY = litFloat(1.f);

        if (j.contains("rotation")) e.rotation = PropValue::deserialize(j["rotation"]);
        else e.rotation = litFloat(0.f);

        if (j.contains("props") && j["props"].is_object())
        {
            for (auto& [k, v] : j["props"].items())
                e.props[k] = PropValue::deserialize(v);
        }

        if (j.contains("children") && j["children"].is_array())
        {
            for (const auto& child : j["children"])
                e.children.push_back(CanvasElement::deserialize(child));
        }

        return e;
    }

    // =====================================================================
    // CanvasConf
    // =====================================================================

    std::string CanvasConf::serialize() const
    {
        nlohmann::json j;
        j["viewportWidth"] = viewportWidth;
        j["viewportHeight"] = viewportHeight;
        j["showGrid"] = showGrid;
        j["gridSizeX"] = gridSizeX;
        j["gridSizeY"] = gridSizeY;
        return j.dump();
    }

    CanvasConf CanvasConf::deserialize(const nlohmann::json& j)
    {
        CanvasConf c;
        c.viewportWidth  = j.value("viewportWidth", 320);
        c.viewportHeight = j.value("viewportHeight", 240);
        c.showGrid       = j.value("showGrid", true);
        c.gridSizeX      = j.value("gridSizeX", 16);
        c.gridSizeY      = j.value("gridSizeY", 16);
        return c;
    }

    // =====================================================================
    // Canvas
    // =====================================================================

    void Canvas::load(const std::string& filePath)
    {
        path = filePath;
        auto text = Utils::FS::loadTextFile(filePath);
        if (text.empty()) return;

        auto j = nlohmann::json::parse(text, nullptr, false);
        if (!j.is_object()) return;

        uuid = j.value("uuid", uint64_t{0});
        if (uuid == 0)
            uuid = Utils::Hash::randomU64();

        name = j.value("name", "");

        if (j.contains("conf"))
            conf = CanvasConf::deserialize(j["conf"]);

        variables.clear();
        if (j.contains("variables") && j["variables"].is_array())
        {
            for (const auto& v : j["variables"])
                variables.push_back(CanvasVariableDef::deserialize(v));
        }

        elements.clear();
        if (j.contains("elements") && j["elements"].is_array())
        {
            for (const auto& e : j["elements"])
                elements.push_back(CanvasElement::deserialize(e));
        }
    }

    std::string Canvas::serialize() const
    {
        nlohmann::json j;
        j["uuid"] = uuid;
        j["name"] = name;
        j["conf"] = nlohmann::json::parse(conf.serialize());

        nlohmann::json varsJson = nlohmann::json::array();
        for (const auto& v : variables)
            varsJson.push_back(nlohmann::json::parse(v.serialize()));
        j["variables"] = varsJson;

        nlohmann::json elemsJson = nlohmann::json::array();
        for (const auto& e : elements)
            elemsJson.push_back(nlohmann::json::parse(e.serialize()));
        j["elements"] = elemsJson;

        return j.dump(2);
    }

    void Canvas::deserialize(const std::string& json)
    {
        auto j = nlohmann::json::parse(json, nullptr, false);
        if (!j.is_object()) return;
        // Preserve path — only restore the logical content
        name = j.value("name", name);
        if (j.contains("conf"))       conf = CanvasConf::deserialize(j["conf"]);
        variables.clear();
        if (j.contains("variables") && j["variables"].is_array())
            for (const auto& v : j["variables"])
                variables.push_back(CanvasVariableDef::deserialize(v));
        elements.clear();
        if (j.contains("elements") && j["elements"].is_array())
            for (const auto& e : j["elements"])
                elements.push_back(CanvasElement::deserialize(e));
    }

    void Canvas::save(const std::string& filePath) const
    {
        Utils::FS::saveTextFile(filePath, serialize());
    }

    const char* Canvas::varTypeToCpp(CanvasVarType t)
    {
        switch (t)
        {
            case CanvasVarType::Float:     return "float";
            case CanvasVarType::Int:       return "int32_t";
            case CanvasVarType::Bool:      return "bool";
            case CanvasVarType::ColorRGBA: return "color_t";
            case CanvasVarType::StringPtr: return "const char*";
            case CanvasVarType::SpriteRef: return "AssetRef<sprite_t>";
            case CanvasVarType::FontRef:   return "int32_t";
            default:                       return "float";
        }
    }

    const char* Canvas::varTypeName(CanvasVarType t)
    {
        switch (t)
        {
            case CanvasVarType::Float:     return "float";
            case CanvasVarType::Int:       return "int";
            case CanvasVarType::Bool:      return "bool";
            case CanvasVarType::ColorRGBA: return "color_t";
            case CanvasVarType::StringPtr: return "string";
            case CanvasVarType::SpriteRef: return "sprite";
            case CanvasVarType::FontRef:   return "font";
            default:                       return "float";
        }
    }
}
