/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../project/canvas/canvas.h"

namespace Editor
{
    class CanvasObjectInspector
    {
    private:
        void drawElementProps(Project::CanvasElement& e,
                              const std::vector<Project::CanvasVariableDef>& vars);

    public:
        void draw(Project::Canvas& canvas, Project::CanvasElement* selected);
    };
}
