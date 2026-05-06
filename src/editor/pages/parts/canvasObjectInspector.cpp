/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "canvasObjectInspector.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../imgui/helper.h"
#include "../../canvasHistory.h"
#include "../../../context.h"
#include <cmath>

static const char* TYPE_NAMES_ELEM[] = {"Sprite","Text","ColorRect","TextureRect"};

// Blend mode options: {display label, C++ constant or "0"}
static constexpr struct { const char* label; const char* constant; } BLEND_MODES[] = {
    {"None",     "0"},
    {"Multiply", "RDPQ_BLENDER_MULTIPLY"},
};
static constexpr int BLEND_MODE_COUNT = 2;

// Combiner options
static constexpr struct { const char* label; const char* constant; } COMBINERS[] = {
    {"TEX",          "RDPQ_COMBINER_TEX"},
    {"TEX * Prim",   "RDPQ_COMBINER1((1,TEX0,PRIM,TEX0),(TEX0,0,PRIM,0))"},
    {"Flat (Prim)",  "RDPQ_COMBINER_FLAT"},
};
static constexpr int COMBINER_COUNT = 3;

namespace
{
    bool parseColor(const nlohmann::json& val, float out[4])
    {
        if (val.is_array() && val.size() == 4) {
            out[0] = val[0].get<int>() / 255.0f;
            out[1] = val[1].get<int>() / 255.0f;
            out[2] = val[2].get<int>() / 255.0f;
            out[3] = val[3].get<int>() / 255.0f;
            return true;
        }
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        return false;
    }

    nlohmann::json serializeColor(const float col[4])
    {
        return nlohmann::json::array({
            (int)(col[0]*255), (int)(col[1]*255),
            (int)(col[2]*255), (int)(col[3]*255)
        });
    }

    // Returns index of currently selected option, or -1
    int findBlendMode(const nlohmann::json& val)
    {
        if (val.is_number_integer() && val.get<int>() == 0) return 0;
        if (val.is_string()) {
            auto s = val.get<std::string>();
            for (int i = 0; i < BLEND_MODE_COUNT; ++i)
                if (s == BLEND_MODES[i].constant) return i;
        }
        return 0;
    }

    int findCombiner(const nlohmann::json& val)
    {
        if (val.is_string()) {
            auto s = val.get<std::string>();
            for (int i = 0; i < COMBINER_COUNT; ++i)
                if (s == COMBINERS[i].constant) return i;
        }
        return 0;
    }

    // Small (?) marker with a tooltip — call after any prop draw
    void helpMarker(const char* desc)
    {
        ImGui::SameLine();
        ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))
            ImGui::SetTooltip("%s", desc);
    }

    // Draw binding badge + unbind button when prop is bound
    bool drawBindBadge(Project::PropValue& p)
    {
        if (!p.isBound) return false;
        if (p.isOp) {
            ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "[%s %s %s]",
                p.opLhs.c_str(), p.opOp.c_str(), p.opRhs.c_str());
        } else {
            ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "[%s]", p.boundVar.c_str());
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("X##ubnd")) {
            p.isBound  = false;
            p.boundVar = {};
            p.isOp     = false;
            p.opLhs = p.opOp = p.opRhs = {};
            p.value    = 0;
            Editor::CanvasHistory::markChanged("Unbind");
        }
        return true;
    }

    // Type-aware literal widget for comparison RHS
    void drawRhsLiteral(Project::CanvasVarType lhsType, std::string& val)
    {
        ImGui::SetNextItemWidth(90.0f);
        switch (lhsType) {
            case Project::CanvasVarType::Float: {
                float f = 0.0f;
                try { f = std::stof(val); } catch (...) {}
                if (ImGui::DragFloat("##rhslit", &f, 0.1f)) {
                    char buf[32]; snprintf(buf, sizeof(buf), "%g", f);
                    val = buf;
                }
            } break;
            case Project::CanvasVarType::Int: {
                int i = 0;
                try { i = std::stoi(val); } catch (...) {}
                if (ImGui::DragInt("##rhslit", &i)) val = std::to_string(i);
            } break;
            case Project::CanvasVarType::Bool: {
                bool b = (val == "true");
                if (ImGui::Checkbox("##rhslit", &b)) val = b ? "true" : "false";
            } break;
            default:
                ImGui::InputText("##rhslit", &val);
                break;
        }
    }

    // Bool-specific bind popup: supports variable reference OR comparison operation
    void drawBoolBindButton(Project::PropValue& p,
                            const std::vector<Project::CanvasVariableDef>& vars)
    {
        static constexpr const char* OPS[] = {">", "<", ">=", "<=", "==", "!="};
        static int         s_mode{0};
        static std::string s_opLhs;
        static int         s_opOpIdx{4}; // default "=="
        static bool        s_opRhsIsVar{false};
        static std::string s_opRhsVar;
        static std::string s_opRhsLit;

        bool open = ImGui::SmallButton("Bind");
        if (open) {
            s_mode = 0;
            s_opLhs.clear(); s_opOpIdx = 4;
            s_opRhsIsVar = false; s_opRhsVar.clear(); s_opRhsLit.clear();
            ImGui::OpenPopup("##boolbind");
        }

        if (ImGui::BeginPopup("##boolbind")) {
            // Mode tabs
            if (ImGui::RadioButton("Variable",  &s_mode, 0)) {}
            ImGui::SameLine();
            if (ImGui::RadioButton("Operation", &s_mode, 1)) {}
            ImGui::Separator();

            if (s_mode == 0) {
                // Direct bool variable
                bool any = false;
                for (const auto& v : vars) {
                    if (v.type != Project::CanvasVarType::Bool) continue;
                    any = true;
                    if (ImGui::MenuItem(v.name.c_str())) {
                        p.isBound  = true;
                        p.isOp     = false;
                        p.boundVar = v.name;
                        Editor::CanvasHistory::markChanged("Bind variable");
                        ImGui::CloseCurrentPopup();
                    }
                }
                if (!any) ImGui::TextDisabled("(no bool variables)");
            } else {
                // Comparison operation builder
                // LHS variable (any type)
                const char* lhsPrev = s_opLhs.empty() ? "(LHS)" : s_opLhs.c_str();
                ImGui::SetNextItemWidth(110.0f);
                if (ImGui::BeginCombo("##oplhs", lhsPrev)) {
                    for (const auto& v : vars) {
                        bool sel = (v.name == s_opLhs);
                        if (ImGui::Selectable(v.name.c_str(), sel)) s_opLhs = v.name;
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                // Operator
                ImGui::SetNextItemWidth(48.0f);
                ImGui::Combo("##opop", &s_opOpIdx, OPS, 6);
                ImGui::SameLine();
                // RHS — variable or literal, with a small [V/L] toggle
                if (s_opRhsIsVar) {
                    // Find LHS type to filter matching variables
                    Project::CanvasVarType lhsType = Project::CanvasVarType::Float;
                    for (const auto& v : vars) if (v.name == s_opLhs) { lhsType = v.type; break; }

                    const char* rhsPrev = s_opRhsVar.empty() ? "(RHS)" : s_opRhsVar.c_str();
                    ImGui::SetNextItemWidth(100.0f);
                    if (ImGui::BeginCombo("##oprhs", rhsPrev)) {
                        for (const auto& v : vars) {
                            if (v.type != lhsType || v.name == s_opLhs) continue;
                            bool sel = (v.name == s_opRhsVar);
                            if (ImGui::Selectable(v.name.c_str(), sel)) s_opRhsVar = v.name;
                            if (sel) ImGui::SetItemDefaultFocus();
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    Project::CanvasVarType lhsType = Project::CanvasVarType::Float;
                    for (const auto& v : vars) if (v.name == s_opLhs) { lhsType = v.type; break; }
                    drawRhsLiteral(lhsType, s_opRhsLit);
                }
                ImGui::SameLine();
                // Toggle RHS between variable and literal
                if (ImGui::SmallButton(s_opRhsIsVar ? "[V]" : "[L]")) {
                    s_opRhsIsVar = !s_opRhsIsVar;
                    s_opRhsVar.clear(); s_opRhsLit.clear();
                }
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("Toggle RHS: Variable / Literal");

                // Apply
                bool valid = !s_opLhs.empty() && (s_opRhsIsVar ? !s_opRhsVar.empty() : !s_opRhsLit.empty());
                if (!valid) ImGui::BeginDisabled();
                if (ImGui::Button("Apply", {-1, 0})) {
                    p.isBound     = true;
                    p.isOp        = true;
                    p.opLhs       = s_opLhs;
                    p.opOp        = OPS[s_opOpIdx];
                    p.opRhsIsVar  = s_opRhsIsVar;
                    p.opRhs       = s_opRhsIsVar ? s_opRhsVar : s_opRhsLit;
                    Editor::CanvasHistory::markChanged("Bind operation");
                    ImGui::CloseCurrentPopup();
                }
                if (!valid) ImGui::EndDisabled();
            }
            ImGui::EndPopup();
        }
    }

    // Draw bind button + popup
    void drawBindButton(Project::PropValue& p,
                        const std::vector<Project::CanvasVariableDef>& vars)
    {
        if (ImGui::SmallButton("Bind")) ImGui::OpenPopup("##bv");
        if (ImGui::BeginPopup("##bv")) {
            ImGui::Text("Bind to variable:");
            ImGui::Separator();
            for (const auto& v : vars) {
                if (ImGui::MenuItem(v.name.c_str())) {
                    p.isBound = true;
                    p.boundVar = v.name;
                    p.value = {};
                    Editor::CanvasHistory::markChanged("Bind variable");
                }
            }
            if (vars.empty()) ImGui::TextDisabled("(no variables)");
            ImGui::EndPopup();
        }
    }

    void rowLabel(const char* label)
    {
        float colW = ImGui::GetContentRegionAvail().x * 0.38f;
        ImGui::Text("%s", label);
        ImGui::SameLine(colW);
    }

    // ----------------------------------------------------------------
    // Per-type prop drawing
    // ----------------------------------------------------------------

    // Full bool prop row: label | checkbox (or bind badge) | Bind button.
    // PushID(label) ensures each instance gets a unique popup ID.
    void drawBoolProp(const char* label, Project::PropValue& p,
                      const std::vector<Project::CanvasVariableDef>& vars,
                      bool defaultVal = false)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            bool val = p.value.is_boolean() ? p.value.get<bool>() : defaultVal;
            if (ImGui::Checkbox("##v", &val)) {
                p.value = val;
                Editor::CanvasHistory::markChanged(std::string("Edit ") + label);
            }
            ImGui::SameLine();
            drawBoolBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawFloatProp(const char* label, Project::PropValue& p,
                       const std::vector<Project::CanvasVariableDef>& vars)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            ImGui::SetNextItemWidth(-80.0f);
            float val = p.value.is_number() ? p.value.get<float>() : 0.f;
            if (ImGui::DragFloat("##v", &val, 0.5f)) {
                p.value = val;
                Editor::CanvasHistory::markChanged(std::string("Edit ") + label);
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    // Integer position — steps by 1, stores rounded whole number
    void drawPosProp(const char* label, Project::PropValue& p,
                     const std::vector<Project::CanvasVariableDef>& vars)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            ImGui::SetNextItemWidth(-80.0f);
            float val = std::round(p.value.is_number() ? p.value.get<float>() : 0.f);
            if (ImGui::DragFloat("##v", &val, 1.0f, -4096.f, 4096.f, "%.0f")) {
                p.value = std::round(val);
                Editor::CanvasHistory::markChanged(std::string("Edit ") + label);
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawIntProp(const char* label, Project::PropValue& p,
                     const std::vector<Project::CanvasVariableDef>& vars)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            ImGui::SetNextItemWidth(-80.0f);
            int val = p.value.is_number() ? p.value.get<int>() : 0;
            if (ImGui::DragInt("##v", &val)) {
                p.value = val;
                Editor::CanvasHistory::markChanged(std::string("Edit ") + label);
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawEnumProp(const char* label, Project::PropValue& p,
                      const std::vector<Project::CanvasVariableDef>& vars,
                      std::initializer_list<const char*> names)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            int val = p.value.is_number() ? p.value.get<int>() : 0;
            int n = (int)names.size();
            val = (val < 0 || val >= n) ? 0 : val;
            const char* cur = *(names.begin() + val);
            ImGui::SetNextItemWidth(-80.0f);
            if (ImGui::BeginCombo("##v", cur)) {
                int i = 0;
                for (const char* name : names) {
                    if (ImGui::Selectable(name, val == i)) {
                        p.value = i;
                        Editor::CanvasHistory::markChanged(std::string("Edit ") + label);
                    }
                    ++i;
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawColorProp(const char* label, Project::PropValue& p,
                       const std::vector<Project::CanvasVariableDef>& vars)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            float col[4]; parseColor(p.value, col);
            if (ImGui::ColorEdit4("##v", col,
                ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                p.value = serializeColor(col);
                Editor::CanvasHistory::markChanged(std::string("Edit ") + label);
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawFontProp(const char* label, Project::PropValue& p,
                      const std::vector<Project::CanvasVariableDef>& vars)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            const auto& fonts = ctx.project->getAssets().getTypeEntries(Project::FileType::FONT);
            int curId = p.value.is_number() ? p.value.get<int>() : 0;

            std::string curName = "(none)";
            for (const auto& f : fonts) {
                if ((int)f.conf.fontId.value == curId) { curName = f.name; break; }
            }

            ImGui::SetNextItemWidth(-80.0f);
            if (ImGui::BeginCombo("##font", curName.c_str())) {
                if (ImGui::Selectable("(none)", curId == 0)) {
                    p.value = 0;
                    Editor::CanvasHistory::markChanged("Edit Font");
                }
                for (const auto& f : fonts) {
                    if (f.conf.fontId.value == 0) continue;
                    int fId = (int)f.conf.fontId.value;
                    bool sel = (fId == curId);
                    if (ImGui::Selectable(f.name.c_str(), sel)) {
                        p.value = fId;
                        Editor::CanvasHistory::markChanged("Edit Font");
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("Font ID: %d", fId);
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawStringProp(const char* label, Project::PropValue& p,
                        const std::vector<Project::CanvasVariableDef>& vars)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            std::string val = p.value.is_string() ? p.value.get<std::string>() : "";
            ImGui::SetNextItemWidth(-80.0f);
            if (ImGui::InputText("##v", &val)) {
                p.value = val;
                Editor::CanvasHistory::markChanged(std::string("Edit ") + label);
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawSpriteProp(const char* label, Project::PropValue& p,
                        const std::vector<Project::CanvasVariableDef>& vars)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            const auto& sprites = ctx.project->getAssets().getTypeEntries(Project::FileType::IMAGE);

            std::string curName = "(none)";
            uint64_t curUUID = p.value.is_number_integer() ? p.value.get<uint64_t>() : 0;
            if (curUUID != 0) {
                auto* entry = ctx.project->getAssets().getEntryByUUID(curUUID);
                if (entry) curName = entry->name;
            }

            ImGui::SetNextItemWidth(-80.0f);
            if (ImGui::BeginCombo("##spr", curName.c_str())) {
                if (ImGui::Selectable("(none)", curUUID == 0)) {
                    p.value = 0;
                    Editor::CanvasHistory::markChanged("Edit Sprite");
                }
                for (const auto& entry : sprites) {
                    bool sel = (entry.getUUID() == curUUID);
                    if (ImGui::Selectable(entry.name.c_str(), sel)) {
                        p.value = (uint64_t)entry.getUUID();
                        Editor::CanvasHistory::markChanged("Edit Sprite");
                    }
                    if (sel) ImGui::SetItemDefaultFocus();
                    if (ImGui::IsItemHovered())
                        ImGui::SetTooltip("%s", entry.romPath.c_str());
                }
                ImGui::EndCombo();
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawBlendModeProp(const char* label, Project::PropValue& p,
                           const std::vector<Project::CanvasVariableDef>& vars)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            int cur = findBlendMode(p.value);
            ImGui::SetNextItemWidth(-80.0f);
            if (ImGui::Combo("##v", &cur, [](void*, int n, const char** out) {
                *out = BLEND_MODES[n].label; return true;
            }, nullptr, BLEND_MODE_COUNT)) {
                if (cur == 0) p.value = 0;
                else p.value = std::string(BLEND_MODES[cur].constant);
                Editor::CanvasHistory::markChanged("Edit Blend Mode");
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawCombinerProp(const char* label, Project::PropValue& p,
                          const std::vector<Project::CanvasVariableDef>& vars)
    {
        ImGui::PushID(label);
        rowLabel(label);
        if (!drawBindBadge(p)) {
            int cur = findCombiner(p.value);
            ImGui::SetNextItemWidth(-80.0f);
            if (ImGui::Combo("##v", &cur, [](void*, int n, const char** out) {
                *out = COMBINERS[n].label; return true;
            }, nullptr, COMBINER_COUNT)) {
                p.value = std::string(COMBINERS[cur].constant);
                Editor::CanvasHistory::markChanged("Edit Combiner");
            }
            ImGui::SameLine();
            drawBindButton(p, vars);
        }
        ImGui::PopID();
    }

    void drawTextArgs(Project::CanvasElement& e,
                      const std::vector<Project::CanvasVariableDef>& vars)
    {
        auto& argsProp = e.props["args"];
        if (!argsProp.value.is_array())
            argsProp.value = nlohmann::json::array();
        auto& arr = argsProp.value;

        ImGui::Text("Arguments(%d)", (int)arr.size());
        ImGui::SameLine();
        if (ImGui::SmallButton("Add")) {
            arr.push_back(std::string(""));
            Editor::CanvasHistory::markChanged("Add argument");
        }
        ImGui::SameLine();
        if (ImGui::SmallButton("Clear") && !arr.empty()) {
            arr = nlohmann::json::array();
            Editor::CanvasHistory::markChanged("Clear arguments");
        }

        int deleteIdx = -1;
        for (int i = 0; i < (int)arr.size(); ++i)
        {
            ImGui::PushID(i);
            ImGui::Indent(16.0f);

            std::string argName = arr[i].is_string() ? arr[i].get<std::string>() : "";
            ImGui::Text("%s", argName.empty() ? "<None>" : argName.c_str());
            ImGui::SameLine();

            if (ImGui::SmallButton("Bind")) ImGui::OpenPopup("##argbind");
            if (ImGui::BeginPopup("##argbind")) {
                ImGui::Text("Bind to variable:");
                ImGui::Separator();
                for (const auto& v : vars) {
                    if (ImGui::MenuItem(v.name.c_str())) {
                        arr[i] = v.name;
                        Editor::CanvasHistory::markChanged("Bind argument");
                    }
                }
                if (vars.empty()) ImGui::TextDisabled("(no variables)");
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton("Del")) deleteIdx = i;

            ImGui::Unindent(16.0f);
            ImGui::PopID();
        }
        if (deleteIdx >= 0) {
            arr.erase(arr.begin() + deleteIdx);
            Editor::CanvasHistory::markChanged("Delete argument");
        }
    }
}

void Editor::CanvasObjectInspector::drawElementProps(
    Project::CanvasElement& e,
    const std::vector<Project::CanvasVariableDef>& vars)
{
    ImGui::Text("Type: %s", TYPE_NAMES_ELEM[static_cast<int>(e.type)]);
    if (ImGui::InputText("Name", &e.name))
        Editor::CanvasHistory::markChanged("Rename element");
    drawBoolProp("Visible", e.visible, vars, /*defaultVal=*/true);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawPosProp  ("X",        e.x,        vars);
        drawPosProp  ("Y",        e.y,        vars);
        drawFloatProp("Rotation", e.rotation, vars);
    }

    if (ImGui::CollapsingHeader("Properties", ImGuiTreeNodeFlags_DefaultOpen))
    {
        switch (e.type)
        {
            case Project::CanvasElementType::Sprite:
                drawSpriteProp   ("Sprite",    e.props["sprite"],       vars);
                drawColorProp    ("Color",     e.props["color"],        vars);
                drawBlendModeProp("Blend",     e.props["blendMode"],    vars);
                drawIntProp      ("Alpha Cmp", e.props["alphaCompare"], vars);
                drawCombinerProp ("Combiner",  e.props["combiner"],     vars);
                if (ImGui::CollapsingHeader("Blit"))
                {
                    drawFloatProp("Scale X", e.props["blit_scale_x"], vars);
                    helpMarker("Horizontal scale (0 = no scaling = 1.0). Negative = flip.");
                    drawFloatProp("Scale Y", e.props["blit_scale_y"], vars);
                    helpMarker("Vertical scale (0 = no scaling = 1.0). Negative = flip.");
                    drawIntProp("Tile",    e.props["blit_tile"], vars);
                    helpMarker("Base tile descriptor (default: TILE_0 = 0)");
                    drawIntProp("S0",      e.props["blit_s0"],   vars);
                    helpMarker("Source sub-rect top-left X coordinate");
                    drawIntProp("T0",      e.props["blit_t0"],   vars);
                    helpMarker("Source sub-rect top-left Y coordinate");
                    drawIntProp("Width",   e.props["blit_w"],    vars);
                    helpMarker("Source sub-rect width (0 = full surface width)");
                    drawIntProp("Height",  e.props["blit_h"],    vars);
                    helpMarker("Source sub-rect height (0 = full surface height)");
                    drawBoolProp("Flip X", e.props["blit_flip_x"], vars);
                    helpMarker("Flip horizontally before all other transformations");
                    drawBoolProp("Flip Y", e.props["blit_flip_y"], vars);
                    helpMarker("Flip vertically before all other transformations");
                    drawIntProp("CX",      e.props["blit_cx"],   vars);
                    helpMarker("Transformation hotspot X, relative to (S0, T0)");
                    drawIntProp("CY",      e.props["blit_cy"],   vars);
                    helpMarker("Transformation hotspot Y, relative to (S0, T0)");
                    drawBoolProp("Allow XForm", e.props["blit_allow_xform"], vars);
                    helpMarker("If true, blit is affected by transforms from rdpq_xform");
                    drawBoolProp("Filtering", e.props["blit_filtering"], vars);
                    helpMarker("Enable texture filtering (activates anti-artifact workaround when splitting textures)");
                }
                break;

            case Project::CanvasElementType::Text:
                drawStringProp("Text",  e.props["text"],  vars);
                drawTextArgs(e, vars);
                drawFontProp  ("Font",  e.props["font"],  vars);
                drawFloatProp ("Scale", e.props["scale"], vars);
                drawColorProp ("Color", e.props["color"], vars);
                if (ImGui::CollapsingHeader("Text Params")) {
                    drawIntProp ("Style ID",     e.props["style"],          vars);
                    helpMarker("Initial style ID for the text");
                    drawIntProp ("Width",        e.props["tp_width"],        vars);
                    helpMarker("Max paragraph width in pixels (0 = unbounded)");
                    drawIntProp ("Height",       e.props["tp_height"],       vars);
                    helpMarker("Max paragraph height in pixels (0 = unbounded)");
                    drawEnumProp("Align",        e.props["tp_align"],        vars, {"Left", "Center", "Right"});
                    helpMarker("Horizontal text alignment");
                    drawEnumProp("VAlign",       e.props["tp_valign"],       vars, {"Top", "Center", "Bottom"});
                    helpMarker("Vertical text alignment");
                    drawIntProp ("Indent",       e.props["tp_indent"],       vars);
                    helpMarker("First-line indentation in pixels (left alignment only)");
                    drawIntProp ("Max Chars",    e.props["tp_max_chars"],    vars);
                    helpMarker("Max characters to print (0 = all), useful for typewriter effect");
                    drawIntProp ("Char Spacing", e.props["tp_char_spacing"], vars);
                    helpMarker("Extra spacing between characters in pixels");
                    drawIntProp ("Line Spacing", e.props["tp_line_spacing"], vars);
                    helpMarker("Extra spacing between lines in pixels");
                    drawEnumProp("Wrap",         e.props["tp_wrap"],         vars, {"None", "Ellipses", "Char", "Word"});
                    helpMarker("Line wrap mode");
                    drawBoolProp("Disable AA Fix",    e.props["tp_disable_aa_fix"],    vars, false);
                    helpMarker("Skip anti-aliasing fix for speedup (when AA is disabled in display_init)");
                    drawBoolProp("Preserve Overlap",  e.props["tp_preserve_overlap"],  vars, false);
                    helpMarker("Preserve overlapping glyphs (forces LTR rendering, impacts performance)");
                }
                break;

            case Project::CanvasElementType::ColorRect:
                drawFloatProp("Width",  e.props["width"],  vars);
                drawFloatProp("Height", e.props["height"], vars);
                drawColorProp("Color",  e.props["color"],  vars);
                break;

            case Project::CanvasElementType::TextureRect:
                drawSpriteProp("Sprite", e.props["sprite"], vars);
                drawFloatProp ("Width",  e.props["width"],  vars);
                drawFloatProp ("Height", e.props["height"], vars);
                drawFloatProp ("S0",     e.props["s0"],     vars);
                drawFloatProp ("T0",     e.props["t0"],     vars);
                drawColorProp ("Color",  e.props["color"],  vars);
                break;
        }
    }
}

void Editor::CanvasObjectInspector::draw(
    Project::Canvas& canvas, Project::CanvasElement* selected)
{
    if (!selected)
    {
        ImGui::TextDisabled("No element selected");
        return;
    }
    drawElementProps(*selected, canvas.variables);
}
