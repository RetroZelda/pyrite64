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

    // Draw binding badge + unbind button when prop is bound
    bool drawBindBadge(Project::PropValue& p)
    {
        if (!p.isBound) return false;
        ImGui::TextColored({0.4f, 0.8f, 1.0f, 1.0f}, "[%s]", p.boundVar.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("X##ubnd")) {
            p.isBound = false;
            p.boundVar = {};
            p.value = 0;
            Editor::CanvasHistory::markChanged("Unbind variable");
        }
        return true;
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
    if (ImGui::Checkbox("Visible", &e.visible))
        Editor::CanvasHistory::markChanged("Toggle visibility");
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen))
    {
        drawPosProp  ("X",        e.x,        vars);
        drawPosProp  ("Y",        e.y,        vars);
        drawFloatProp("Scale X",  e.scaleX,   vars);
        drawFloatProp("Scale Y",  e.scaleY,   vars);
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
                break;

            case Project::CanvasElementType::Text:
                drawStringProp("Text",  e.props["text"],  vars);
                drawTextArgs(e, vars);
                drawFontProp  ("Font",  e.props["font"],  vars);
                drawFloatProp ("Scale", e.props["scale"], vars);
                drawColorProp ("Color", e.props["color"], vars);
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
