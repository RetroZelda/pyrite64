/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../project/canvas/canvas.h"

namespace Editor
{
    // Editor panel to author a canvas's named events. Declaration order is the
    // stable enum value (the ObjectEvent.type wire value) emitted by the builder.
    class CanvasEvents
    {
    public:
        void draw(Project::Canvas& canvas);
    };
}
