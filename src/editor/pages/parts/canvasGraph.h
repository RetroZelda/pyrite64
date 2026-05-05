/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../project/canvas/canvas.h"

namespace Editor
{
    class CanvasGraph
    {
    private:
        uint64_t selectedUUID{0};

        void drawElement(Project::Canvas& canvas, Project::CanvasElement& e, int depth);
        void addDefaultProps(Project::CanvasElement& e);

    public:
        uint64_t getSelectedUUID() const { return selectedUUID; }
        void setSelectedUUID(uint64_t uuid) { selectedUUID = uuid; }

        // Returns pointer to selected element, or nullptr
        Project::CanvasElement* getSelected(Project::Canvas& canvas);

        void draw(Project::Canvas& canvas);
    };
}
