/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "canvasSettings.h"
#include "imgui.h"
#include "../../imgui/helper.h"
#include "../../canvasHistory.h"

void Editor::CanvasSettings::draw(Project::Canvas& canvas)
{
    if (ImGui::CollapsingHeader("Canvas", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImTable::start("Canvas");
        if (ImTable::add("Viewport W", canvas.conf.viewportWidth))
            Editor::CanvasHistory::markChanged("Edit viewport");
        if (ImTable::add("Viewport H", canvas.conf.viewportHeight))
            Editor::CanvasHistory::markChanged("Edit viewport");
        ImTable::end();
    }

    if (ImGui::CollapsingHeader("Grid", ImGuiTreeNodeFlags_DefaultOpen))
    {
        ImTable::start("Grid");
        if (ImTable::add("Show Grid", canvas.conf.showGrid))
            Editor::CanvasHistory::markChanged("Toggle grid");
        if (ImTable::add("Grid X",    canvas.conf.gridSizeX))
            Editor::CanvasHistory::markChanged("Edit grid");
        if (ImTable::add("Grid Y",    canvas.conf.gridSizeY))
            Editor::CanvasHistory::markChanged("Edit grid");
        ImTable::end();
    }
}
