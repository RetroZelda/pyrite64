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
    // CanvasEventDef
    // =====================================================================

    std::string CanvasEventDef::serialize() const
    {
        nlohmann::json j;
        j["name"] = name;
        return j.dump();
    }

    CanvasEventDef CanvasEventDef::deserialize(const nlohmann::json& j)
    {
        CanvasEventDef e;
        e.name = j.value("name", "");
        return e;
    }

    // =====================================================================
    // PropValue
    // =====================================================================

    std::string PropValue::serialize() const
    {
        nlohmann::json j;
        if (isIndex) {
            j["isBound"] = true;
            j["isIndex"] = true;
            return j.dump();
        }
        if (isItemField) {
            j["isBound"]   = true;
            j["isItemField"] = true;
            j["itemField"] = itemField;
            return j.dump();
        }
        j["isBound"] = isBound;
        if (isBound) {
            if (isOp) {
                j["isOp"]       = true;
                j["opLhs"]      = opLhs;
                j["opOp"]       = opOp;
                j["opRhsIsVar"] = opRhsIsVar;
                j["opRhs"]      = opRhs;
            } else {
                j["var"] = boundVar;
            }
        } else {
            j["val"] = value;
        }
        return j.dump();
    }

    PropValue PropValue::deserialize(const nlohmann::json& j)
    {
        PropValue p;
        p.isBound = j.value("isBound", false);
        if (p.isBound) {
            if (j.value("isIndex", false)) {
                p.isIndex = true;
            } else if (j.value("isItemField", false)) {
                p.isItemField = true;
                p.itemField   = j.value("itemField", "");
            } else if (j.value("isOp", false)) {
                p.isOp       = true;
                p.opLhs      = j.value("opLhs", "");
                p.opOp       = j.value("opOp", ">");
                p.opRhsIsVar = j.value("opRhsIsVar", false);
                p.opRhs      = j.value("opRhs", "");
            } else {
                p.boundVar = j.value("var", "");
            }
        } else {
            p.value = j.contains("val") ? j["val"] : nlohmann::json{};
        }
        return p;
    }

    // =====================================================================
    // CanvasItemField / CanvasRepeater
    // =====================================================================

    std::string CanvasItemField::serialize() const
    {
        nlohmann::json j;
        j["name"] = name;
        j["type"] = static_cast<int>(type);
        return j.dump();
    }

    CanvasItemField CanvasItemField::deserialize(const nlohmann::json& j)
    {
        CanvasItemField f;
        f.name = j.value("name", "");
        f.type = static_cast<CanvasVarType>(j.value("type", 0));
        return f;
    }

    std::string CanvasRepeater::serialize() const
    {
        nlohmann::json j;
        j["enabled"]  = enabled;
        j["countVar"] = countVar;
        j["maxCount"] = maxCount;
        j["columns"]  = columns;
        j["spacingX"] = spacingX;
        j["spacingY"] = spacingY;
        nlohmann::json fields = nlohmann::json::array();
        for (const auto& f : itemFields)
            fields.push_back(nlohmann::json::parse(f.serialize()));
        j["itemFields"] = fields;
        return j.dump();
    }

    CanvasRepeater CanvasRepeater::deserialize(const nlohmann::json& j)
    {
        CanvasRepeater r;
        r.enabled  = j.value("enabled", false);
        r.countVar = j.value("countVar", "");
        r.maxCount = j.value("maxCount", 8);
        r.columns  = j.value("columns", 1);
        r.spacingX = j.value("spacingX", 16.0f);
        r.spacingY = j.value("spacingY", 16.0f);
        if (j.contains("itemFields") && j["itemFields"].is_array())
            for (const auto& f : j["itemFields"])
                r.itemFields.push_back(CanvasItemField::deserialize(f));
        return r;
    }

    // =====================================================================
    // Animation timeline (Phase 5)
    // =====================================================================

    std::string CanvasKeyframe::serialize() const
    {
        nlohmann::json j;
        j["time"]   = time;
        j["value"]  = value;
        j["easing"] = static_cast<int>(easing);
        return j.dump();
    }

    CanvasKeyframe CanvasKeyframe::deserialize(const nlohmann::json& j)
    {
        CanvasKeyframe k;
        k.time   = j.value("time", 0.0f);
        k.value  = j.contains("value") ? j["value"] : nlohmann::json{};
        k.easing = static_cast<CanvasEaseType>(j.value("easing", 0));
        return k;
    }

    std::string CanvasTimelineTrack::serialize() const
    {
        nlohmann::json j;
        j["targetUuid"] = targetUuid;
        j["channel"]    = static_cast<int>(channel);
        j["varName"]    = varName;
        nlohmann::json ks = nlohmann::json::array();
        for (const auto& k : keys) ks.push_back(nlohmann::json::parse(k.serialize()));
        j["keys"] = ks;
        return j.dump();
    }

    CanvasTimelineTrack CanvasTimelineTrack::deserialize(const nlohmann::json& j)
    {
        CanvasTimelineTrack t;
        t.targetUuid = j.value("targetUuid", uint64_t{0});
        t.channel    = static_cast<CanvasTrackChannel>(j.value("channel", 0));
        t.varName    = j.value("varName", "");
        if (j.contains("keys") && j["keys"].is_array())
            for (const auto& k : j["keys"])
                t.keys.push_back(CanvasKeyframe::deserialize(k));
        return t;
    }

    std::string CanvasTimelineEvent::serialize() const
    {
        nlohmann::json j;
        j["time"]       = time;
        j["action"]     = static_cast<int>(action);
        j["eventName"]  = eventName;
        j["targetAnim"] = targetAnim;
        j["seekTime"]   = seekTime;
        return j.dump();
    }

    CanvasTimelineEvent CanvasTimelineEvent::deserialize(const nlohmann::json& j)
    {
        CanvasTimelineEvent e;
        e.time       = j.value("time", 0.0f);
        e.action     = static_cast<Action>(j.value("action", 0));
        e.eventName  = j.value("eventName", "");
        e.targetAnim = j.value("targetAnim", -1);
        e.seekTime   = j.value("seekTime", 0.0f);
        return e;
    }

    std::string CanvasAnimation::serialize() const
    {
        nlohmann::json j;
        j["name"]       = name;
        j["duration"]   = duration;
        j["autoPlay"]   = autoPlay;
        j["timeSource"] = timeSource;
        j["timeVar"]    = timeVar;
        nlohmann::json ts = nlohmann::json::array();
        for (const auto& t : tracks) ts.push_back(nlohmann::json::parse(t.serialize()));
        j["tracks"] = ts;
        nlohmann::json es = nlohmann::json::array();
        for (const auto& e : events) es.push_back(nlohmann::json::parse(e.serialize()));
        j["events"] = es;
        return j.dump();
    }

    CanvasAnimation CanvasAnimation::deserialize(const nlohmann::json& j)
    {
        CanvasAnimation a;
        a.name       = j.value("name", "");
        a.duration   = j.value("duration", 1.0f);
        a.autoPlay   = j.value("autoPlay", false);
        a.timeSource = j.value("timeSource", 0);
        a.timeVar    = j.value("timeVar", "");
        if (j.contains("tracks") && j["tracks"].is_array())
            for (const auto& t : j["tracks"])
                a.tracks.push_back(CanvasTimelineTrack::deserialize(t));
        if (j.contains("events") && j["events"].is_array())
            for (const auto& e : j["events"])
                a.events.push_back(CanvasTimelineEvent::deserialize(e));
        return a;
    }

    namespace
    {
        nlohmann::json serializeAnims(const std::vector<CanvasAnimation>& anims)
        {
            nlohmann::json arr = nlohmann::json::array();
            for (const auto& a : anims) arr.push_back(nlohmann::json::parse(a.serialize()));
            return arr;
        }
        void deserializeAnims(const nlohmann::json& j, std::vector<CanvasAnimation>& out)
        {
            out.clear();
            if (j.is_array())
                for (const auto& a : j) out.push_back(CanvasAnimation::deserialize(a));
        }
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
        if (scriptUuid != 0) j["scriptUuid"] = scriptUuid;
        j["x"] = nlohmann::json::parse(x.serialize());
        j["y"] = nlohmann::json::parse(y.serialize());
        j["rotation"] = nlohmann::json::parse(rotation.serialize());
        j["visible"] = nlohmann::json::parse(visible.serialize());

        nlohmann::json propsJson = nlohmann::json::object();
        for (const auto& [k, v] : props)
            propsJson[k] = nlohmann::json::parse(v.serialize());
        j["props"] = propsJson;

        if (repeater.enabled)
            j["repeater"] = nlohmann::json::parse(repeater.serialize());

        if (!animations.empty())
            j["animations"] = serializeAnims(animations);

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
        e.scriptUuid = j.value("scriptUuid", uint64_t{0});
        if (j.contains("visible")) {
            auto& jv = j["visible"];
            if (jv.is_boolean()) e.visible.value = jv.get<bool>(); // backward compat
            else e.visible = PropValue::deserialize(jv);
        } else { e.visible.value = true; }

        if (j.contains("x")) e.x = PropValue::deserialize(j["x"]);
        else e.x = litFloat(0.f);

        if (j.contains("y")) e.y = PropValue::deserialize(j["y"]);
        else e.y = litFloat(0.f);

        if (j.contains("rotation")) e.rotation = PropValue::deserialize(j["rotation"]);
        else e.rotation = litFloat(0.f);

        if (j.contains("props") && j["props"].is_object())
        {
            for (auto& [k, v] : j["props"].items())
                e.props[k] = PropValue::deserialize(v);
        }

        if (j.contains("repeater"))
            e.repeater = CanvasRepeater::deserialize(j["repeater"]);

        if (j.contains("animations"))
            deserializeAnims(j["animations"], e.animations);

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

        events.clear();
        if (j.contains("events") && j["events"].is_array())
        {
            for (const auto& e : j["events"])
                events.push_back(CanvasEventDef::deserialize(e));
        }

        if (j.contains("animations"))
            deserializeAnims(j["animations"], animations);

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

        nlohmann::json eventsJson = nlohmann::json::array();
        for (const auto& e : events)
            eventsJson.push_back(nlohmann::json::parse(e.serialize()));
        j["events"] = eventsJson;

        j["animations"] = serializeAnims(animations);

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
        events.clear();
        if (j.contains("events") && j["events"].is_array())
            for (const auto& e : j["events"])
                events.push_back(CanvasEventDef::deserialize(e));
        if (j.contains("animations"))
            deserializeAnims(j["animations"], animations);
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
