/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "canvasVariables.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../imgui/helper.h"
#include "../../canvasHistory.h"
#include "../../../context.h"

static constexpr const char* TYPE_NAMES[] = {
    "float", "int", "bool", "color_t", "string", "sprite", "font"
};

namespace
{
    bool parseColor(const std::string& s, float out[4])
    {
        int r = 255, g = 255, b = 255, a = 255;
        if (sscanf(s.c_str(), "{%d, %d, %d, %d}", &r, &g, &b, &a) == 4) {
            out[0] = r / 255.0f; out[1] = g / 255.0f;
            out[2] = b / 255.0f; out[3] = a / 255.0f;
            return true;
        }
        out[0] = out[1] = out[2] = out[3] = 1.0f;
        return false;
    }

    std::string serializeColor(const float col[4])
    {
        return "{" + std::to_string((int)(col[0]*255)) + ", "
                   + std::to_string((int)(col[1]*255)) + ", "
                   + std::to_string((int)(col[2]*255)) + ", "
                   + std::to_string((int)(col[3]*255)) + "}";
    }

    void drawDefaultWidget(Project::CanvasVariableDef& v)
    {
        ImGui::SetNextItemWidth(-1);
        ImGui::PushID("defval");

        switch (v.type)
        {
            case Project::CanvasVarType::Float:
            {
                float val = 0.0f;
                try { val = std::stof(v.defaultValue); } catch (...) {}
                if (ImGui::DragFloat("##f", &val, 0.01f)) {
                    char buf[32];
                    snprintf(buf, sizeof(buf), "%.6gf", val);
                    v.defaultValue = buf;
                    Editor::CanvasHistory::markChanged("Edit variable default");
                }
            } break;

            case Project::CanvasVarType::Int:
            {
                int val = 0;
                try { val = std::stoi(v.defaultValue); } catch (...) {}
                if (ImGui::DragInt("##i", &val)) {
                    v.defaultValue = std::to_string(val);
                    Editor::CanvasHistory::markChanged("Edit variable default");
                }
            } break;

            case Project::CanvasVarType::Bool:
            {
                bool val = (v.defaultValue == "true");
                if (ImGui::Checkbox("##b", &val)) {
                    v.defaultValue = val ? "true" : "false";
                    Editor::CanvasHistory::markChanged("Edit variable default");
                }
            } break;

            case Project::CanvasVarType::ColorRGBA:
            {
                float col[4];
                parseColor(v.defaultValue, col);
                if (ImGui::ColorEdit4("##c", col,
                    ImGuiColorEditFlags_NoInputs | ImGuiColorEditFlags_AlphaPreviewHalf)) {
                    v.defaultValue = serializeColor(col);
                    Editor::CanvasHistory::markChanged("Edit variable default");
                }
            } break;

            case Project::CanvasVarType::StringPtr:
            {
                if (ImGui::InputText("##s", &v.defaultValue))
                    Editor::CanvasHistory::markChanged("Edit variable default");
            } break;

            case Project::CanvasVarType::SpriteRef:
            {
                const auto& sprites = ctx.project->getAssets().getTypeEntries(Project::FileType::IMAGE);
                std::string curName = v.defaultValue.empty() ? "(none)" : v.defaultValue;
                if (ImGui::BeginCombo("##spr", curName.c_str()))
                {
                    if (ImGui::Selectable("(none)", v.defaultValue.empty())) {
                        v.defaultValue = "";
                        Editor::CanvasHistory::markChanged("Edit variable default");
                    }
                    for (const auto& entry : sprites)
                    {
                        if (entry.outPath.empty()) continue;
                        auto romPath = entry.romPath;
                        if (romPath.starts_with("rom:/")) romPath = romPath.substr(5);
                        bool sel = (v.defaultValue == romPath);
                        if (ImGui::Selectable(entry.name.c_str(), sel)) {
                            v.defaultValue = romPath;
                            Editor::CanvasHistory::markChanged("Edit variable default");
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
            } break;

            case Project::CanvasVarType::FontRef:
            {
                int curId = 0;
                try { curId = std::stoi(v.defaultValue); } catch (...) {}
                const auto& fonts = ctx.project->getAssets().getTypeEntries(Project::FileType::FONT);
                std::string curName = v.defaultValue.empty() ? "(none)" : v.defaultValue;
                for (const auto& f : fonts)
                    if ((int)f.conf.fontId.value == curId) { curName = f.name; break; }
                if (ImGui::BeginCombo("##font", curName.c_str())) {
                    if (ImGui::Selectable("(none)", curId == 0)) {
                        v.defaultValue = "0";
                        Editor::CanvasHistory::markChanged("Edit variable default");
                    }
                    for (const auto& f : fonts) {
                        if (f.conf.fontId.value == 0) continue;
                        int fId = (int)f.conf.fontId.value;
                        bool sel = (fId == curId);
                        if (ImGui::Selectable(f.name.c_str(), sel)) {
                            v.defaultValue = std::to_string(fId);
                            Editor::CanvasHistory::markChanged("Edit variable default");
                        }
                        if (sel) ImGui::SetItemDefaultFocus();
                        if (ImGui::IsItemHovered())
                            ImGui::SetTooltip("Font ID: %d", fId);
                    }
                    ImGui::EndCombo();
                }
            } break;

            default:
                ImGui::TextDisabled("(unsupported)");
                break;
        }

        ImGui::PopID();
    }

    const char* defaultForType(Project::CanvasVarType t)
    {
        switch (t) {
            case Project::CanvasVarType::Float:     return "0.0f";
            case Project::CanvasVarType::Int:       return "0";
            case Project::CanvasVarType::Bool:      return "false";
            case Project::CanvasVarType::ColorRGBA: return "{255, 255, 255, 255}";
            case Project::CanvasVarType::StringPtr: return "";
            case Project::CanvasVarType::SpriteRef: return "";
            case Project::CanvasVarType::FontRef:   return "0";
            default:                                return "";
        }
    }
}

void Editor::CanvasVariables::draw(Project::Canvas& canvas)
{
    ImGui::Text("Variables (%zu)", canvas.variables.size());
    ImGui::Separator();

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 3));
    if (ImGui::BeginTable("##vars", 4,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, -ImGui::GetFrameHeightWithSpacing())))
    {
        ImGui::TableSetupColumn("Name",    ImGuiTableColumnFlags_WidthStretch, 0.38f);
        ImGui::TableSetupColumn("Type",    ImGuiTableColumnFlags_WidthStretch, 0.20f);
        ImGui::TableSetupColumn("Default", ImGuiTableColumnFlags_WidthStretch, 0.34f);
        ImGui::TableSetupColumn("##del",   ImGuiTableColumnFlags_WidthFixed,   20.0f);
        ImGui::TableHeadersRow();

        int deleteIdx = -1;
        for (int i = 0; i < (int)canvas.variables.size(); ++i)
        {
            auto& v = canvas.variables[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);

            ImGui::TableSetColumnIndex(0);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##name", &v.name))
                Editor::CanvasHistory::markChanged("Rename variable");

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            int typeIdx = static_cast<int>(v.type);
            if (ImGui::Combo("##type", &typeIdx, TYPE_NAMES, 7)) {
                v.type = static_cast<Project::CanvasVarType>(typeIdx);
                v.defaultValue = defaultForType(v.type);
                Editor::CanvasHistory::markChanged("Change variable type");
            }

            ImGui::TableSetColumnIndex(2);
            drawDefaultWidget(v);

            ImGui::TableSetColumnIndex(3);
            if (ImGui::SmallButton("X"))
                deleteIdx = i;

            ImGui::PopID();
        }

        if (deleteIdx >= 0) {
            canvas.variables.erase(canvas.variables.begin() + deleteIdx);
            Editor::CanvasHistory::markChanged("Delete variable");
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    if (ImGui::Button("+ Add Variable"))
    {
        Project::CanvasVariableDef def;
        def.name = "newVar";
        def.type = Project::CanvasVarType::Float;
        def.defaultValue = "0.0f";
        canvas.variables.push_back(def);
        Editor::CanvasHistory::markChanged("Add variable");
    }
}
