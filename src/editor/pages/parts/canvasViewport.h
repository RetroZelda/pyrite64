/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../project/canvas/canvas.h"
#include "imgui.h"
#include <unordered_map>
#include <vector>
#include <utility>
#include <cstdint>

namespace Editor
{
    // Per-element animation offsets for live timeline preview (set by CanvasTimeline).
    struct CanvasAnimOverride {
        float dx{0.f}, dy{0.f}, drot{0.f}, scale{1.f};
        float alpha{1.f};                                  // 0..1 multiplier on the element's alpha
        uint8_t color[4]{255, 255, 255, 255};              // animated Color channel (RGBA 0..255)
        bool hasColor{false};                              // a Color track is driving this element
    };

    class CanvasViewport
    {
    private:
        float zoom{2.0f};
        ImVec2 panOffset{0.f, 0.f};

        std::unordered_map<uint64_t, CanvasAnimOverride> animOverrides{};

        bool isDragging{false};
        bool isPanning{false};
        ImVec2 dragStart{};

        uint64_t selectedUUID{0};          // primary selection (inspector + graph sync)
        std::vector<uint64_t> selection_;  // full multi-selection set (always contains the primary)
        bool selectionChanged{false};

        // Per-element start positions captured when a (possibly multi-element) drag begins.
        std::vector<std::pair<uint64_t, ImVec2>> dragStarts_;

        ImVec2 canvasToScreen(ImVec2 canvasPos, ImVec2 origin) const;
        ImVec2 screenToCanvas(ImVec2 screenPos, ImVec2 origin) const;

        void drawGrid(ImDrawList* dl, ImVec2 origin, const Project::CanvasConf& conf);
        void drawElement(ImDrawList* dl, ImVec2 origin,
                         const Project::CanvasElement& e, bool parentVisible,
                         const Project::CanvasConf& conf);
        void handleInput(ImVec2 origin, Project::Canvas& canvas);
        void drawAlignBar(Project::Canvas& canvas);

        bool isSelected(uint64_t uuid) const;
        // Top-level element (x,y,w,h) in canvas space — shared by hit-test, draw, alignment.
        ImVec4 elementRect(const Project::Canvas& canvas, const Project::CanvasElement& e) const;

        Project::CanvasElement* findElement(std::vector<Project::CanvasElement>& elems,
                                             uint64_t uuid);

        // Returns canvas-space (w, h) for a Text element, fitting the 8px preview text.
        // If tp_width/tp_height are explicitly set those take priority.
        // Minimum 8x8 canvas pixels.
        ImVec2 calcTextElementBounds(const Project::CanvasElement& e) const;

    public:
        void draw(Project::Canvas& canvas);

        // Called every frame by the editor to sync from the graph. No-op when unchanged so a
        // viewport multi-selection isn't clobbered by the per-frame pull.
        void setSelected(uint64_t uuid) {
            if (uuid == selectedUUID) return;
            selectedUUID = uuid;
            selection_.clear();
            if (uuid) selection_.push_back(uuid);
        }
        uint64_t getSelected() const { return selectedUUID; }
        bool consumeSelectionChange() { bool c = selectionChanged; selectionChanged = false; return c; }

        // Live timeline preview: applied to element positions/scale while drawing.
        void setAnimOverrides(const std::unordered_map<uint64_t, CanvasAnimOverride>& o) { animOverrides = o; }
        void clearAnimOverrides() { animOverrides.clear(); }
    };
}
