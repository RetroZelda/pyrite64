/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <cstdint>
#include <string>

namespace Project { struct Canvas; }

namespace Editor::CanvasHistory
{
    void begin(Project::Canvas* canvas);
    void end();

    void markChanged(std::string reason);

    bool undo();
    bool redo();
    bool canUndo();
    bool canRedo();

    std::string getUndoDescription();
    std::string getRedoDescription();

    uint32_t getUndoCount();
    uint32_t getRedoCount();
    uint64_t getMemoryUsage();

    void clear();
}
