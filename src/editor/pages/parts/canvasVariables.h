/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../project/canvas/canvas.h"

namespace Editor
{
    class CanvasVariables
    {
    private:
        int editingIdx{-1};

    public:
        void draw(Project::Canvas& canvas);
    };
}
