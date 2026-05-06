/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../project/canvas/canvas.h"
#include "imgui.h"

namespace Editor
{
    class CanvasViewport
    {
    private:
        float zoom{2.0f};
        ImVec2 panOffset{0.f, 0.f};

        bool isDragging{false};
        bool isPanning{false};
        ImVec2 dragStart{};
        ImVec2 dragObjStart{};

        uint64_t selectedUUID{0};
        bool selectionChanged{false};

        ImVec2 canvasToScreen(ImVec2 canvasPos, ImVec2 origin) const;
        ImVec2 screenToCanvas(ImVec2 screenPos, ImVec2 origin) const;

        void drawGrid(ImDrawList* dl, ImVec2 origin, const Project::CanvasConf& conf);
        void drawElement(ImDrawList* dl, ImVec2 origin,
                         const Project::CanvasElement& e, bool parentVisible,
                         const Project::CanvasConf& conf);
        void handleInput(ImVec2 origin, Project::Canvas& canvas);

        Project::CanvasElement* findElement(std::vector<Project::CanvasElement>& elems,
                                             uint64_t uuid);

        // Returns canvas-space (w, h) for a Text element, fitting the 8px preview text.
        // If tp_width/tp_height are explicitly set those take priority.
        // Minimum 8x8 canvas pixels.
        ImVec2 calcTextElementBounds(const Project::CanvasElement& e) const;

    public:
        void draw(Project::Canvas& canvas);

        void setSelected(uint64_t uuid) { selectedUUID = uuid; }
        uint64_t getSelected() const { return selectedUUID; }
        bool consumeSelectionChange() { bool c = selectionChanged; selectionChanged = false; return c; }
    };
}
