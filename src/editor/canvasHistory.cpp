/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "canvasHistory.h"
#include "../project/canvas/canvas.h"

namespace
{
    struct Entry
    {
        std::string state;
        std::string description;
    };

    Project::Canvas* gCanvas{nullptr};
    std::string gCaptured;       // state snapshotted at begin()
    std::string gNextReason;

    std::vector<Entry> gUndo;
    std::vector<Entry> gRedo;

    constexpr size_t MAX_SIZE = 100;
}

namespace Editor::CanvasHistory
{
    void begin(Project::Canvas* canvas)
    {
        gCanvas = canvas;
        if (!canvas) return;

        gCaptured = canvas->serialize();

        if (gUndo.empty())
            gUndo.push_back({gCaptured, "Initial"});
    }

    void end()
    {
        if (gNextReason.empty() || !gCanvas) {
            gCanvas = nullptr;
            return;
        }

        std::string newState = gCanvas->serialize();

        // Skip if nothing actually changed
        if (!gUndo.empty() && gUndo.back().state == newState) {
            gNextReason.clear();
            gCanvas = nullptr;
            return;
        }

        gRedo.clear();
        gUndo.push_back({std::move(newState), std::move(gNextReason)});

        if (gUndo.size() > MAX_SIZE)
            gUndo.erase(gUndo.begin(), gUndo.end() - MAX_SIZE);

        gNextReason.clear();
        gCanvas = nullptr;
    }

    void markChanged(std::string reason)
    {
        gNextReason = std::move(reason);
    }

    bool undo()
    {
        if (!canUndo() || !gCanvas) return false;

        auto top = std::move(gUndo.back());
        gUndo.pop_back();
        gCanvas->deserialize(gUndo.back().state);
        gRedo.push_back(std::move(top));
        return true;
    }

    bool redo()
    {
        if (!canRedo() || !gCanvas) return false;

        auto top = std::move(gRedo.back());
        gRedo.pop_back();
        gCanvas->deserialize(top.state);
        gUndo.push_back(std::move(top));
        return true;
    }

    bool canUndo() { return gUndo.size() > 1; }
    bool canRedo() { return !gRedo.empty(); }

    std::string getUndoDescription()
    {
        if (gUndo.empty()) return "";
        return gUndo.back().description;
    }

    std::string getRedoDescription()
    {
        if (gRedo.empty()) return "";
        return gRedo.back().description;
    }

    uint32_t getUndoCount() { return (uint32_t)gUndo.size(); }
    uint32_t getRedoCount() { return (uint32_t)gRedo.size(); }

    uint64_t getMemoryUsage()
    {
        uint64_t total = 0;
        for (const auto& e : gUndo) total += e.state.size() + e.description.size();
        for (const auto& e : gRedo) total += e.state.size() + e.description.size();
        return total;
    }

    void clear()
    {
        gUndo.clear();
        gRedo.clear();
        gNextReason.clear();
        gCanvas = nullptr;
    }
}
