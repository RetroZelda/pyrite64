/**
* @copyright 2026 - Max Bebök
* @license MIT
*/
#include "canvasTimeline.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../canvasHistory.h"
#include "../../../../n64/engine/include/ui/uiEval.h"  // shared ease/lerp — same math as the cart

#include <vector>
#include <string>
#include <algorithm>
#include <cstdio>

namespace
{
    using namespace Project;

    const char* CHANNEL_NAMES[] = {
        "X", "Y", "Rotation", "Scale", "Alpha", "Color", "Variable", "FrameIndex"
    };
    const char* EASE_NAMES[] = { "Linear", "EaseOutCubic", "EaseInOutCubic", "EaseOutSin" };

    // Flatten the element tree into (uuid, name) for the track target picker.
    void flattenElems(const std::vector<CanvasElement>& elems, int depth,
                      std::vector<std::pair<uint64_t, std::string>>& out)
    {
        for (const auto& e : elems) {
            out.push_back({ e.uuid, std::string(depth * 2, ' ') + e.name });
            flattenElems(e.children, depth + 1, out);
        }
    }

    const char* elemName(const std::vector<std::pair<uint64_t, std::string>>& flat, uint64_t uuid)
    {
        for (const auto& p : flat) if (p.first == uuid) return p.second.c_str();
        return "(none)";
    }

    // Phase 5e: locate an element by uuid (for element-scoped clip authoring).
    CanvasElement* findElem(std::vector<CanvasElement>& elems, uint64_t uuid)
    {
        for (auto& e : elems) {
            if (e.uuid == uuid) return &e;
            if (auto* r = findElem(e.children, uuid)) return r;
        }
        return nullptr;
    }

    // Flatten one element + its subtree (element-scoped clips may target the subtree).
    void flattenSubtree(const CanvasElement& e, int depth,
                        std::vector<std::pair<uint64_t, std::string>>& out)
    {
        out.push_back({ e.uuid, std::string(depth * 2, ' ') + e.name });
        flattenElems(e.children, depth + 1, out);
    }

    // Evaluate a scalar track at time `et`. Mirrors the generated runtime evalTrack
    // (uiEval ease/lerp) so the editor preview matches the cart.
    float evalTrackEditor(const Project::CanvasTimelineTrack& t, float et)
    {
        const auto& k = t.keys;
        if (k.empty()) return 0.f;
        auto val = [](const Project::CanvasKeyframe& kf) {
            return kf.value.is_number() ? kf.value.get<float>() : 0.f;
        };
        if (et <= k.front().time) return val(k.front());
        if (et >= k.back().time)  return val(k.back());
        for (size_t i = 0; i + 1 < k.size(); ++i) {
            if (et >= k[i].time && et < k[i+1].time) {
                float lt = (k[i+1].time > k[i].time)
                         ? (et - k[i].time) / (k[i+1].time - k[i].time) : 0.f;
                return P64::UI::Eval::lerp(val(k[i]), val(k[i+1]),
                                           P64::UI::Eval::applyEase((int)k[i].easing, lt));
            }
        }
        return val(k.back());
    }

    // Evaluate a 4-component Color track at `et` -> RGBA 0..255. Mirrors the generated
    // runtime evalTrackColor so the preview tint matches the cart.
    void evalTrackEditorColor(const Project::CanvasTimelineTrack& t, float et, uint8_t out[4])
    {
        const auto& k = t.keys;
        auto comp = [](const Project::CanvasKeyframe& kf, int i) -> float {
            if (kf.value.is_array() && i < (int)kf.value.size() && kf.value[i].is_number())
                return kf.value[i].get<float>();
            return 255.f;
        };
        if (k.empty())            { for (int i=0;i<4;++i) out[i]=255; return; }
        if (et <= k.front().time) { for (int i=0;i<4;++i) out[i]=(uint8_t)comp(k.front(),i); return; }
        if (et >= k.back().time)  { for (int i=0;i<4;++i) out[i]=(uint8_t)comp(k.back(),i);  return; }
        for (size_t i = 0; i + 1 < k.size(); ++i) {
            if (et >= k[i].time && et < k[i+1].time) {
                float lt = (k[i+1].time > k[i].time) ? (et - k[i].time) / (k[i+1].time - k[i].time) : 0.f;
                float ez = P64::UI::Eval::applyEase((int)k[i].easing, lt);
                for (int c=0;c<4;++c) out[c]=(uint8_t)P64::UI::Eval::lerp(comp(k[i],c), comp(k[i+1],c), ez);
                return;
            }
        }
        for (int i=0;i<4;++i) out[i]=(uint8_t)comp(k.back(),i);
    }
}

void Editor::CanvasTimeline::draw(Project::Canvas& canvas, uint64_t selectedUuid)
{
    previewOverrides.clear();  // rebuilt each frame for the active clip at scrubTime

    // --- Scope: canvas-scoped clips, or a repeater element's per-instance clips (Phase 5e) ---
    // Element scope is only meaningful for a repeater (each instance animates independently);
    // a non-repeated element should use a canvas clip instead.
    Project::CanvasElement* elem = selectedUuid ? findElem(canvas.elements, selectedUuid) : nullptr;
    bool canElem = elem && elem->repeater.enabled;
    if (!canElem) scope = 0;  // no eligible element -> force canvas scope
    ImGui::TextUnformatted("Scope:"); ImGui::SameLine();
    // Switching the clip list (scope/element) must invalidate clip + track/key/event selection,
    // since those indices reference whichever vector was active.
    auto resetSel = [&]{ activeClip = -1; selTrack = -1; selKey = -1; selEvent = -1; };
    if (ImGui::RadioButton("Canvas", scope == 0)) { scope = 0; resetSel(); }
    ImGui::SameLine();
    {
        if (!canElem) ImGui::BeginDisabled();
        std::string elabel = std::string("Element: ") + (elem ? elem->name : "(repeater)");
        if (ImGui::RadioButton(elabel.c_str(), scope == 1)) { scope = 1; resetSel(); }
        if (!canElem) ImGui::EndDisabled();
        if (!canElem && ImGui::IsItemHovered())
            ImGui::SetTooltip("Select a repeater element to author per-instance clips");
    }
    if (scope == 1 && elem && elem->uuid != scopeElem) { scopeElem = elem->uuid; resetSel(); }
    ImGui::Separator();

    // The clip list for the current scope.
    std::vector<Project::CanvasAnimation>& anims =
        (scope == 1 && elem) ? elem->animations : canvas.animations;

    // --- Clip selector / new / delete ---
    ImGui::Text("Animations (%zu)", anims.size());
    if (activeClip >= (int)anims.size()) activeClip = -1;

    std::string cur = (activeClip >= 0) ? anims[activeClip].name : "(select clip)";
    ImGui::SetNextItemWidth(160);
    if (ImGui::BeginCombo("##clip", cur.c_str())) {
        for (int i = 0; i < (int)anims.size(); ++i)
            if (ImGui::Selectable(anims[i].name.c_str(), i == activeClip)) {
                activeClip = i; scrubTime = 0.0f; playing = false;
            }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ New")) {
        Project::CanvasAnimation a; a.name = "Anim" + std::to_string(anims.size());
        anims.push_back(a);
        activeClip = (int)anims.size() - 1;
        Editor::CanvasHistory::markChanged("Add animation");
    }
    if (activeClip >= 0) {
        ImGui::SameLine();
        if (ImGui::SmallButton("Delete")) {
            anims.erase(anims.begin() + activeClip);
            activeClip = -1;
            Editor::CanvasHistory::markChanged("Delete animation");
            return;
        }
        // Move the selected clip between canvas scope and the selected repeater's
        // per-instance clips (transfers the whole clip; keyframes/tracks preserved).
        if (scope == 0 && canElem) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Move to Elem")) {
                elem->animations.push_back(anims[activeClip]);
                anims.erase(anims.begin() + activeClip);
                scope = 1; scopeElem = elem->uuid;
                activeClip = (int)elem->animations.size() - 1;
                selTrack = selKey = selEvent = -1;
                Editor::CanvasHistory::markChanged("Move animation to element");
                return;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Make this a per-instance clip on '%s'", elem->name.c_str());
        } else if (scope == 1 && elem) {
            ImGui::SameLine();
            if (ImGui::SmallButton("Move to Canvas")) {
                canvas.animations.push_back(anims[activeClip]);
                anims.erase(anims.begin() + activeClip);
                scope = 0;
                activeClip = (int)canvas.animations.size() - 1;
                selTrack = selKey = selEvent = -1;
                Editor::CanvasHistory::markChanged("Move animation to canvas");
                return;
            }
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Make this a canvas-scoped clip");
        }
    }
    ImGui::Separator();

    if (activeClip < 0) { ImGui::TextDisabled("No clip selected."); return; }
    auto& clip = anims[activeClip];

    // --- Clip properties (compact) ---
    ImGui::SetNextItemWidth(140);
    if (ImGui::InputText("Name##clip", &clip.name)) Editor::CanvasHistory::markChanged("Rename clip");
    ImGui::SameLine(); ImGui::SetNextItemWidth(90);
    if (ImGui::DragFloat("Dur", &clip.duration, 0.05f, 0.05f, 60.0f, "%.2fs")) Editor::CanvasHistory::markChanged("Clip duration");
    ImGui::SameLine();
    if (ImGui::Checkbox("Auto", &clip.autoPlay)) Editor::CanvasHistory::markChanged("Clip autoplay");

    // Time source (Part B): Playback advances elapsed (play/seek); Driven samples a value each
    // frame as time-in-seconds (clamped to [0,Dur]) so gameplay sets the pose directly.
    ImGui::SetNextItemWidth(100);
    const char* TIME_SRC[] = { "Playback", "Driven" };
    if (ImGui::Combo("Time", &clip.timeSource, TIME_SRC, 2)) Editor::CanvasHistory::markChanged("Clip time source");
    if (clip.timeSource == 1) {
        ImGui::SameLine(); ImGui::SetNextItemWidth(150);
        if (ImGui::BeginCombo("by##timevar", clip.timeVar.empty() ? "(pick value)" : clip.timeVar.c_str())) {
            for (const auto& v : canvas.variables)
                if (v.type == Project::CanvasVarType::Float || v.type == Project::CanvasVarType::Int)
                    if (ImGui::Selectable(("var: " + v.name).c_str(), v.name == clip.timeVar)) { clip.timeVar = v.name; Editor::CanvasHistory::markChanged("Clip time var"); }
            if (scope == 1 && elem)
                for (const auto& f : elem->repeater.itemFields)
                    if (f.type == Project::CanvasVarType::Float || f.type == Project::CanvasVarType::Int)
                        if (ImGui::Selectable(("item: " + f.name).c_str(), f.name == clip.timeVar)) { clip.timeVar = f.name; Editor::CanvasHistory::markChanged("Clip time field"); }
            ImGui::EndCombo();
        }
        ImGui::SameLine(); ImGui::TextDisabled("(?)");
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("Driven: clip time = this value in seconds, clamped to [0, Dur].\nSet Dur=1 to drive with a 0..1 value. Scrub here to preview poses.");
    }

    // --- Toolbar ---
    if (ImGui::Button(playing ? "Pause" : "Play")) playing = !playing;
    ImGui::SameLine();
    if (ImGui::Button("|<")) { scrubTime = 0.0f; playing = false; }
    ImGui::SameLine(); ImGui::Text("t=%.2f", scrubTime);
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Track")) {
        clip.tracks.push_back({}); selTrack = (int)clip.tracks.size() - 1; selKey = -1;
        Editor::CanvasHistory::markChanged("Add track");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("+ Event")) {
        Project::CanvasTimelineEvent ev; ev.time = scrubTime;
        clip.events.push_back(ev); selEvent = (int)clip.events.size() - 1;
        Editor::CanvasHistory::markChanged("Add event");
    }

    if (playing) {
        scrubTime += ImGui::GetIO().DeltaTime;
        if (scrubTime > clip.duration) scrubTime = 0.0f; // loop the preview
    }
    if (scrubTime > clip.duration) scrubTime = clip.duration;

    std::vector<std::pair<uint64_t, std::string>> flat;
    if (scope == 1 && elem) flattenSubtree(*elem, 0, flat);   // element clips target the subtree
    else                    flattenElems(canvas.elements, 0, flat);

    // Keep each track's keyframes sorted by time (eval + codegen require it).
    for (auto& tr : clip.tracks)
        std::stable_sort(tr.keys.begin(), tr.keys.end(),
            [](const Project::CanvasKeyframe& a, const Project::CanvasKeyframe& b){ return a.time < b.time; });

    // Re-resolve a time-anchored selection (e.g. after dragging a key across a
    // neighbour, which re-sorts the track) so it keeps following that same key.
    if (pendingSelTrack >= 0 && pendingSelTrack < (int)clip.tracks.size()) {
        const auto& ks = clip.tracks[pendingSelTrack].keys;
        int best = -1; float bestD = 1e30f;
        for (int i = 0; i < (int)ks.size(); ++i) {
            float d = ks[i].time - pendingSelTime; if (d < 0.f) d = -d;
            if (d < bestD) { bestD = d; best = i; }
        }
        if (best >= 0) { selTrack = pendingSelTrack; selKey = best; }
        pendingSelTrack = -1;
    }

    // ================= Graphical timeline =================
    const float leftW  = 120.f;
    const float rulerH = 18.f;
    const float trackH = 22.f;
    const float evLaneH = 18.f;
    const int   nTracks = (int)clip.tracks.size();

    // The lane box fills the window's remaining height (minus room for the
    // keyframe/event inspector below) and scrolls internally only if the lanes
    // exceed that height — rather than letting the whole Timeline window scroll.
    // Reserve exactly the inspector footer's measured height (from last frame) so the
    // lane box fills the rest and the footer never makes the whole window scroll.
    float bottomReserve = footerH > 0.f ? footerH + 6.f : 0.f;
    float childH = ImGui::GetContentRegionAvail().y - bottomReserve;
    if (childH < 80.f) childH = 80.f;

    ImGui::BeginChild("tlcanvas", ImVec2(0, childH), true);
    {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 o = ImGui::GetCursorScreenPos();
        float fullW = ImGui::GetContentRegionAvail().x;
        float laneX0 = o.x + leftW;
        float laneW  = fullW - leftW - 4.f; if (laneW < 10.f) laneW = 10.f;
        float dur    = clip.duration > 0.001f ? clip.duration : 1.f;
        auto timeToX = [&](float t){ return laneX0 + (t / dur) * laneW; };
        auto xToTime = [&](float x){ float t = (x - laneX0) / laneW * dur; return t < 0 ? 0 : (t > dur ? dur : t); };

        // Ruler
        dl->AddRectFilled(o, {o.x + fullW, o.y + rulerH}, IM_COL32(40, 40, 46, 255));
        for (int s = 0; s <= 10; ++s) {
            float t = dur * (float)s / 10.f;
            float x = timeToX(t);
            dl->AddLine({x, o.y}, {x, o.y + rulerH}, IM_COL32(85, 85, 95, 255));
            char lbl[16]; snprintf(lbl, sizeof lbl, "%.2f", t);
            dl->AddText({x + 2, o.y + 3}, IM_COL32(150, 150, 160, 255), lbl);
        }

        // Track lanes
        int delTrack = -1;
        for (int ti = 0; ti < nTracks; ++ti) {
            auto& tr = clip.tracks[ti];
            float rowY = o.y + rulerH + ti * trackH;
            dl->AddRectFilled({o.x, rowY}, {o.x + fullW, rowY + trackH},
                              (ti & 1) ? IM_COL32(34, 34, 40, 255) : IM_COL32(28, 28, 34, 255));
            std::string lbl = std::string(CHANNEL_NAMES[(int)tr.channel]) + ":"
                + (tr.channel == Project::CanvasTrackChannel::Variable
                   ? (tr.varName.empty() ? "(var)" : tr.varName) : elemName(flat, tr.targetUuid));
            dl->AddText({o.x + 4, rowY + 4}, (ti == selTrack) ? IM_COL32(255, 220, 120, 255) : IM_COL32(200, 200, 210, 255), lbl.c_str());

            // Label area: click selects + opens config popup
            ImGui::PushID(ti);
            ImGui::SetCursorScreenPos({o.x, rowY});
            if (ImGui::InvisibleButton("lbl", {leftW, trackH})) { selTrack = ti; selKey = -1; ImGui::OpenPopup("trkcfg"); }
            if (ImGui::BeginPopup("trkcfg")) {
                int ch = (int)tr.channel;
                ImGui::SetNextItemWidth(130);
                if (ImGui::Combo("Channel", &ch, CHANNEL_NAMES, IM_ARRAYSIZE(CHANNEL_NAMES))) {
                    tr.channel = (Project::CanvasTrackChannel)ch; Editor::CanvasHistory::markChanged("Track channel");
                }
                if (tr.channel == Project::CanvasTrackChannel::Variable) {
                    ImGui::SetNextItemWidth(130);
                    if (ImGui::BeginCombo("Target", tr.varName.empty() ? "(var)" : tr.varName.c_str())) {
                        if (scope == 1 && elem) {
                            // Element/repeater scope: a Variable track drives a per-instance item field.
                            for (const auto& f : elem->repeater.itemFields)
                                if (f.type == Project::CanvasVarType::Float || f.type == Project::CanvasVarType::Int)
                                    if (ImGui::Selectable(f.name.c_str(), f.name == tr.varName)) { tr.varName = f.name; Editor::CanvasHistory::markChanged("Track item field"); }
                        } else {
                            for (const auto& v : canvas.variables)
                                if (ImGui::Selectable(v.name.c_str(), v.name == tr.varName)) { tr.varName = v.name; Editor::CanvasHistory::markChanged("Track var"); }
                        }
                        ImGui::EndCombo();
                    }
                } else {
                    ImGui::SetNextItemWidth(130);
                    if (ImGui::BeginCombo("Target", elemName(flat, tr.targetUuid))) {
                        for (const auto& p : flat)
                            if (ImGui::Selectable(p.second.c_str(), p.first == tr.targetUuid)) { tr.targetUuid = p.first; Editor::CanvasHistory::markChanged("Track target"); }
                        ImGui::EndCombo();
                    }
                }
                if (ImGui::SmallButton("Delete track")) { delTrack = ti; ImGui::CloseCurrentPopup(); }
                ImGui::EndPopup();
            }

            // Lane area: double-click adds a keyframe. AllowOverlap so the keyframe
            // diamonds submitted on top of this lane stay clickable (otherwise the
            // lane swallows the clicks and keyframes can never be (re)selected).
            ImGui::SetCursorScreenPos({laneX0, rowY});
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("lane", {laneW, trackH});
            if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
                Project::CanvasKeyframe k; k.time = xToTime(ImGui::GetMousePos().x);
                k.value = (tr.channel == Project::CanvasTrackChannel::Color)
                        ? nlohmann::json::array({255, 255, 255, 255}) : nlohmann::json(0.f);
                // Insert at the sorted position so the top-of-frame re-sort won't move it,
                // then select that index immediately (highlighted this frame).
                int ins = 0; while (ins < (int)tr.keys.size() && tr.keys[ins].time < k.time) ++ins;
                tr.keys.insert(tr.keys.begin() + ins, k);
                selTrack = ti; selKey = ins;
                Editor::CanvasHistory::markChanged("Add keyframe");
            }

            // Keyframe diamonds (submitted after lane so they win hit-testing)
            for (int ki = 0; ki < (int)tr.keys.size(); ++ki) {
                auto& k = tr.keys[ki];
                float kx = timeToX(k.time), ky = rowY + trackH * 0.5f;
                bool sel = (ti == selTrack && ki == selKey);
                ImU32 dcol = sel ? IM_COL32(255, 220, 120, 255) : IM_COL32(120, 180, 255, 255);
                dl->AddQuadFilled({kx, ky - 5}, {kx + 5, ky}, {kx, ky + 5}, {kx - 5, ky}, dcol);
                ImGui::PushID(1000 + ki);
                ImGui::SetCursorScreenPos({kx - 5, ky - 5});
                ImGui::SetNextItemAllowOverlap();
                ImGui::InvisibleButton("kf", {10, 10});
                if (ImGui::IsItemActivated()) { selTrack = ti; selKey = ki; }
                if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                    k.time = xToTime(ImGui::GetMousePos().x);
                    pendingSelTrack = ti; pendingSelTime = k.time;  // keep this key selected after re-sort
                    Editor::CanvasHistory::markChanged("Move keyframe");
                }
                ImGui::PopID();
            }
            ImGui::PopID();
        }
        if (delTrack >= 0) { clip.tracks.erase(clip.tracks.begin() + delTrack); selTrack = -1; selKey = -1; pendingSelTrack = -1; Editor::CanvasHistory::markChanged("Delete track"); }

        // Event lane
        float evY = o.y + rulerH + nTracks * trackH;
        dl->AddRectFilled({o.x, evY}, {o.x + fullW, evY + evLaneH}, IM_COL32(46, 36, 36, 255));
        dl->AddText({o.x + 4, evY + 3}, IM_COL32(200, 160, 160, 255), "events");
        for (int ei = 0; ei < (int)clip.events.size(); ++ei) {
            auto& ev = clip.events[ei];
            float ex = timeToX(ev.time), ey = evY + evLaneH * 0.5f;
            ImU32 ecol = (ev.action == Project::CanvasTimelineEvent::Action::Seek)
                       ? IM_COL32(120, 220, 160, 255) : IM_COL32(230, 150, 120, 255);
            if (ei == selEvent) ecol = IM_COL32(255, 220, 120, 255);
            dl->AddTriangleFilled({ex - 4, ey - 5}, {ex + 4, ey - 5}, {ex, ey + 5}, ecol);
            ImGui::PushID(3000 + ei);
            ImGui::SetCursorScreenPos({ex - 5, evY});
            ImGui::SetNextItemAllowOverlap();
            ImGui::InvisibleButton("evm", {10, evLaneH});
            if (ImGui::IsItemActivated()) selEvent = ei;
            if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
                ev.time = xToTime(ImGui::GetMousePos().x); Editor::CanvasHistory::markChanged("Move event");
            }
            ImGui::PopID();
        }

        // Playhead + ruler-strip scrub
        float phx = timeToX(scrubTime);
        dl->AddLine({phx, o.y}, {phx, evY + evLaneH}, IM_COL32(255, 80, 80, 255), 1.5f);
        ImGui::SetCursorScreenPos({laneX0, o.y});
        ImGui::InvisibleButton("scrub", {laneW, rulerH});
        if (ImGui::IsItemActive()) { scrubTime = xToTime(ImGui::GetMousePos().x); playing = false; }

        // Mark the true content extent (the ImDrawList lanes don't advance the
        // cursor) so the child's ScrollY engages only when the lanes overflow.
        ImGui::SetCursorScreenPos({o.x, evY + evLaneH + 2.f});
        ImGui::Dummy(ImVec2(1.f, 1.f));
    }
    ImGui::EndChild();

    // Live preview: per-element offsets at scrubTime (viewport applies these next frame).
    for (const auto& tr : clip.tracks) {
        if (tr.targetUuid == 0) continue;
        auto& ov = previewOverrides[tr.targetUuid];
        switch (tr.channel) {
            case Project::CanvasTrackChannel::X:        ov.dx    = evalTrackEditor(tr, scrubTime); break;
            case Project::CanvasTrackChannel::Y:        ov.dy    = evalTrackEditor(tr, scrubTime); break;
            case Project::CanvasTrackChannel::Rotation: ov.drot  = evalTrackEditor(tr, scrubTime); break;
            case Project::CanvasTrackChannel::Scale:    ov.scale = evalTrackEditor(tr, scrubTime); break;
            case Project::CanvasTrackChannel::Alpha: {
                float a = evalTrackEditor(tr, scrubTime);
                ov.alpha = a < 0.f ? 0.f : (a > 1.f ? 1.f : a);
                break;
            }
            case Project::CanvasTrackChannel::Color: {
                evalTrackEditorColor(tr, scrubTime, ov.color);
                ov.hasColor = true;
                break;
            }
            default: break; // Variable / FrameIndex not shown in the abstract rect view
        }
    }

    // --- Inspector footer (its measured height is reserved above so it never scrolls) ---
    float footerY0 = ImGui::GetCursorPosY();

    // --- Selected keyframe inspector ---
    if (selTrack >= 0 && selTrack < (int)clip.tracks.size()) {
        auto& tr = clip.tracks[selTrack];
        if (selKey >= 0 && selKey < (int)tr.keys.size()) {
            ImGui::Separator();
            auto& k = tr.keys[selKey];
            ImGui::Text("Keyframe"); ImGui::SameLine();
            if (ImGui::SmallButton("Delete##k")) {
                tr.keys.erase(tr.keys.begin() + selKey); selKey = -1; Editor::CanvasHistory::markChanged("Delete keyframe");
            } else {
                ImGui::SetNextItemWidth(90);
                if (ImGui::DragFloat("Time##ki", &k.time, 0.02f, 0.f, clip.duration, "%.2f")) Editor::CanvasHistory::markChanged("Keyframe time");
                ImGui::SameLine();
                if (tr.channel == Project::CanvasTrackChannel::Color) {
                    // Color keyframe value is an [r,g,b,a] array (0..255).
                    float rgba[4] = {1.f, 1.f, 1.f, 1.f};
                    if (k.value.is_array())
                        for (int i = 0; i < 4 && i < (int)k.value.size(); ++i)
                            if (k.value[i].is_number()) rgba[i] = k.value[i].get<float>() / 255.f;
                    ImGui::SetNextItemWidth(180);
                    if (ImGui::ColorEdit4("Color##ki", rgba, ImGuiColorEditFlags_NoInputs)) {
                        k.value = nlohmann::json::array({ (int)(rgba[0]*255.f + 0.5f), (int)(rgba[1]*255.f + 0.5f),
                                                          (int)(rgba[2]*255.f + 0.5f), (int)(rgba[3]*255.f + 0.5f) });
                        Editor::CanvasHistory::markChanged("Keyframe color");
                    }
                } else if (tr.channel == Project::CanvasTrackChannel::Alpha) {
                    // Alpha is a 0..1 multiplier; constrain authoring to match the runtime clamp.
                    float val = k.value.is_number() ? k.value.get<float>() : 1.f;
                    ImGui::SetNextItemWidth(90);
                    if (ImGui::DragFloat("Value##ki", &val, 0.01f, 0.f, 1.f, "%.2f")) { k.value = val; Editor::CanvasHistory::markChanged("Keyframe value"); }
                } else {
                    float val = k.value.is_number() ? k.value.get<float>() : 0.f;
                    ImGui::SetNextItemWidth(90);
                    if (ImGui::DragFloat("Value##ki", &val, 0.1f)) { k.value = val; Editor::CanvasHistory::markChanged("Keyframe value"); }
                }
                int ez = (int)k.easing; ImGui::SetNextItemWidth(140);
                if (ImGui::Combo("Easing##ki", &ez, EASE_NAMES, IM_ARRAYSIZE(EASE_NAMES))) { k.easing = (Project::CanvasEaseType)ez; Editor::CanvasHistory::markChanged("Keyframe easing"); }
            }
        }
    }

    // --- Selected event inspector ---
    if (selEvent >= 0 && selEvent < (int)clip.events.size()) {
        ImGui::Separator();
        auto& ev = clip.events[selEvent];
        ImGui::Text("Event"); ImGui::SameLine();
        if (ImGui::SmallButton("Delete##ev")) {
            clip.events.erase(clip.events.begin() + selEvent); selEvent = -1; Editor::CanvasHistory::markChanged("Delete event");
        } else {
            ImGui::SetNextItemWidth(90);
            if (ImGui::DragFloat("Time##evi", &ev.time, 0.02f, 0.f, clip.duration, "%.2f")) Editor::CanvasHistory::markChanged("Event time");
            int act = (int)ev.action; const char* ACTS[] = { "FireEvent", "Seek" };
            ImGui::SameLine(); ImGui::SetNextItemWidth(110);
            if (ImGui::Combo("Action##evi", &act, ACTS, 2)) { ev.action = (Project::CanvasTimelineEvent::Action)act; Editor::CanvasHistory::markChanged("Event action"); }
            if (ev.action == Project::CanvasTimelineEvent::Action::FireEvent) {
                ImGui::SetNextItemWidth(140);
                std::string en = ev.eventName.empty() ? "(event)" : ev.eventName;
                if (ImGui::BeginCombo("Name##evi", en.c_str())) {
                    for (const auto& e : canvas.events)
                        if (ImGui::Selectable(e.name.c_str(), e.name == ev.eventName)) { ev.eventName = e.name; Editor::CanvasHistory::markChanged("Event name"); }
                    ImGui::EndCombo();
                }
            } else {
                ImGui::SetNextItemWidth(90);
                if (ImGui::DragFloat("Seek to##evi", &ev.seekTime, 0.02f, 0.f, clip.duration, "%.2f")) Editor::CanvasHistory::markChanged("Seek time");
            }
        }
    }

    // Record the footer's height for next frame's reserve (0 if nothing was drawn).
    footerH = ImGui::GetCursorPosY() - footerY0;
}
