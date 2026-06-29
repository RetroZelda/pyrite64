/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "canvasEvents.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../canvasHistory.h"

void Editor::CanvasEvents::draw(Project::Canvas& canvas)
{
    // Fixed ID (###) so the count in the label doesn't reset the open/closed state.
    std::string header = "Events (" + std::to_string(canvas.events.size()) + ")###canvasEvents";
    if (!ImGui::CollapsingHeader(header.c_str()))
        return;

    ImGui::TextDisabled("Named events gameplay sends into this canvas.");
    ImGui::TextDisabled("Components receive them; gameplay never receives.");

    ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(4, 3));
    // Content-bounded height (capped) so this can stack with other sections in a shared window.
    int evRows = (int)canvas.events.size();
    float evTblH = ImGui::GetFrameHeightWithSpacing() * ((evRows > 8 ? 8 : evRows) + 1.5f);
    if (ImGui::BeginTable("##events", 3,
        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_ScrollY,
        ImVec2(0, evTblH)))
    {
        ImGui::TableSetupColumn("ID",    ImGuiTableColumnFlags_WidthFixed, 28.0f);
        ImGui::TableSetupColumn("Name",  ImGuiTableColumnFlags_WidthStretch);
        ImGui::TableSetupColumn("##del", ImGuiTableColumnFlags_WidthFixed, 20.0f);
        ImGui::TableHeadersRow();

        int deleteIdx = -1;
        for (int i = 0; i < (int)canvas.events.size(); ++i)
        {
            auto& e = canvas.events[i];
            ImGui::TableNextRow();
            ImGui::PushID(i);

            // The row index is the enum value (wire ObjectEvent.type).
            ImGui::TableSetColumnIndex(0);
            ImGui::Text("%d", i);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Event id (ObjectEvent.type). Deleting an event renumbers later ones.");

            ImGui::TableSetColumnIndex(1);
            ImGui::SetNextItemWidth(-1);
            if (ImGui::InputText("##name", &e.name))
                Editor::CanvasHistory::markChanged("Rename event");

            ImGui::TableSetColumnIndex(2);
            if (ImGui::SmallButton("X"))
                deleteIdx = i;

            ImGui::PopID();
        }

        if (deleteIdx >= 0) {
            canvas.events.erase(canvas.events.begin() + deleteIdx);
            Editor::CanvasHistory::markChanged("Delete event");
        }

        ImGui::EndTable();
    }
    ImGui::PopStyleVar();

    if (ImGui::Button("+ Add Event"))
    {
        Project::CanvasEventDef def;
        def.name = "NewEvent";
        canvas.events.push_back(def);
        Editor::CanvasHistory::markChanged("Add event");
    }
}
