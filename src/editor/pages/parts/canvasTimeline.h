/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#pragma once
#include "../../../project/canvas/canvas.h"
#include "canvasViewport.h"   // CanvasAnimOverride
#include <unordered_map>

namespace Editor
{
    // Editor panel for authoring a canvas's animation clips (Phase 5):
    // clips -> tracks (element + channel + keyframes) + timeline events.
    class CanvasTimeline
    {
    private:
        int   activeClip{-1};
        float scrubTime{0.0f};
        bool  playing{false};
        int   selTrack{-1};   // selected track / keyframe / event in the timeline view
        int   selKey{-1};
        int   selEvent{-1};

        // Clip scope (Phase 5e): 0 = canvas-scoped clips, 1 = the selected element's clips.
        int      scope{0};
        uint64_t scopeElem{0};  // element the Element-scope clips were last bound to

        // Selection anchored by time so it survives the per-frame keyframe re-sort
        // (e.g. while dragging a key across a neighbour). Resolved to selKey next frame.
        int   pendingSelTrack{-1};
        float pendingSelTime{0.0f};

        // Measured height of the keyframe/event inspector footer (last frame); the lane
        // box reserves exactly this so the footer never makes the window scroll.
        float footerH{0.0f};

        // Per-element offsets for the current scrub time (fed to the viewport preview).
        std::unordered_map<uint64_t, CanvasAnimOverride> previewOverrides{};

    public:
        void draw(Project::Canvas& canvas, uint64_t selectedUuid = 0);

        // Live-preview offsets at the current scrub time (parity with the runtime eval).
        const std::unordered_map<uint64_t, CanvasAnimOverride>& getPreviewOverrides() const { return previewOverrides; }
    };
}
