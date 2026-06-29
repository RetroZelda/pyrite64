/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "canvasViewport.h"
#include "imgui.h"
#include "imgui_internal.h"
#include "../../../context.h"
#include "../../canvasHistory.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr ImU32 COL_CANVAS_BG    = IM_COL32(30, 30, 30, 255);
    constexpr ImU32 COL_CANVAS_BORD  = IM_COL32(200, 200, 200, 255);
    constexpr ImU32 COL_GRID_MINOR   = IM_COL32(60, 60, 60, 255);
    constexpr ImU32 COL_SPRITE_ELEM  = IM_COL32(80, 140, 220, 160);
    constexpr ImU32 COL_TEXT_ELEM    = IM_COL32(220, 180, 80, 160);
    constexpr ImU32 COL_RECT_ELEM    = IM_COL32(80, 220, 120, 160);
    constexpr ImU32 COL_SEL_OUTLINE  = IM_COL32(255, 200, 50, 255);

    ImU32 elemColor(Project::CanvasElementType t)
    {
        switch (t)
        {
            case Project::CanvasElementType::Sprite:      return COL_SPRITE_ELEM;
            case Project::CanvasElementType::Text:        return COL_TEXT_ELEM;
            case Project::CanvasElementType::ColorRect:   return COL_RECT_ELEM;
            case Project::CanvasElementType::TextureRect: return COL_SPRITE_ELEM;
            default:                                      return COL_SPRITE_ELEM;
        }
    }

    float propLitFloat(const Project::PropValue& p, float def)
    {
        if (!p.isBound && p.value.is_number())
            return p.value.get<float>();
        return def;
    }

    // True if `uuid` is a descendant of `e` (checks e's subtree, not e itself).
    bool elementContains(const Project::CanvasElement& e, uint64_t uuid)
    {
        for (const auto& c : e.children) {
            if (c.uuid == uuid || elementContains(c, uuid)) return true;
        }
        return false;
    }

    void shiftDescendants(std::vector<Project::CanvasElement>& children, float dx, float dy)
    {
        for (auto& child : children)
        {
            if (!child.x.isBound && child.x.value.is_number())
                child.x.value = std::round(child.x.value.get<float>() + dx);
            if (!child.y.isBound && child.y.value.is_number())
                child.y.value = std::round(child.y.value.get<float>() + dy);
            shiftDescendants(child.children, dx, dy);
        }
    }
}

ImVec2 Editor::CanvasViewport::canvasToScreen(ImVec2 cp, ImVec2 origin) const
{
    return {origin.x + panOffset.x + cp.x * zoom,
            origin.y + panOffset.y + cp.y * zoom};
}

ImVec2 Editor::CanvasViewport::screenToCanvas(ImVec2 sp, ImVec2 origin) const
{
    return {(sp.x - origin.x - panOffset.x) / zoom,
            (sp.y - origin.y - panOffset.y) / zoom};
}

void Editor::CanvasViewport::drawGrid(ImDrawList* dl, ImVec2 origin,
                                       const Project::CanvasConf& conf)
{
    if (!conf.showGrid) return;

    auto tl = canvasToScreen({0, 0}, origin);
    auto br = canvasToScreen({(float)conf.viewportWidth, (float)conf.viewportHeight}, origin);

    float gx = conf.gridSizeX * zoom;
    float gy = conf.gridSizeY * zoom;

    for (float x = tl.x; x <= br.x; x += gx)
        dl->AddLine({x, tl.y}, {x, br.y}, COL_GRID_MINOR);
    for (float y = tl.y; y <= br.y; y += gy)
        dl->AddLine({tl.x, y}, {br.x, y}, COL_GRID_MINOR);
}

void Editor::CanvasViewport::drawElement(ImDrawList* dl, ImVec2 origin,
                                          const Project::CanvasElement& e, bool parentVisible,
                                          const Project::CanvasConf& conf)
{
    bool visLit = e.visible.isBound || (e.visible.value.is_boolean() ? e.visible.value.get<bool>() : true);
    if (!visLit || !parentVisible) return;

    float ex = propLitFloat(e.x, 0.f);
    float ey = propLitFloat(e.y, 0.f);

    // Live timeline preview: shift/scale by this element's animation override.
    // Per-element (matches runtime: a parent's offset does not move its children).
    float   animScale    = 1.f;
    float   animAlpha    = 1.f;
    bool    animHasColor = false;
    uint8_t animCol[4]   = {255, 255, 255, 255};
    if (auto ov = animOverrides.find(e.uuid); ov != animOverrides.end())
    {
        ex += ov->second.dx;
        ey += ov->second.dy;
        animScale    = ov->second.scale;
        animAlpha    = ov->second.alpha;
        animHasColor = ov->second.hasColor;
        for (int i = 0; i < 4; ++i) animCol[i] = ov->second.color[i];
    }

    // Apply the animated Color override (replace) and Alpha (multiply) to a draw color,
    // so the preview shows fade/tint the same way the cart composites prim color.
    auto applyTint = [&](ImU32 base) -> ImU32 {
        ImU32 c = animHasColor ? IM_COL32(animCol[0], animCol[1], animCol[2], animCol[3]) : base;
        unsigned a = (c >> IM_COL32_A_SHIFT) & 0xFF;
        a = (unsigned)((float)a * animAlpha);
        return (c & ~((ImU32)0xFF << IM_COL32_A_SHIFT)) | (a << IM_COL32_A_SHIFT);
    };

    float w = (float)conf.gridSizeX;
    float h = (float)conf.gridSizeY;

    switch (e.type)
    {
        case Project::CanvasElementType::Sprite:
        {
            auto spit = e.props.find("sprite");
            if (spit != e.props.end() && !spit->second.isBound &&
                spit->second.value.is_number_integer())
            {
                uint64_t sprUUID = spit->second.value.get<uint64_t>();
                if (sprUUID && ctx.project)
                {
                    auto* entry = ctx.project->getAssets().getEntryByUUID(sprUUID);
                    if (entry && entry->texture)
                    {
                        w = (float)entry->texture->getWidth();
                        h = (float)entry->texture->getHeight();
                    }
                }
            }
            {
                auto sxi = e.props.find("blit_scale_x");
                auto syi = e.props.find("blit_scale_y");
                float scx = (sxi != e.props.end()) ? propLitFloat(sxi->second, 0.f) : 0.f;
                float scy = (syi != e.props.end()) ? propLitFloat(syi->second, 0.f) : 0.f;
                if (scx != 0.f) w *= scx;
                if (scy != 0.f) h *= scy;
            }
            break;
        }
        case Project::CanvasElementType::Text:
        {
            auto bounds = calcTextElementBounds(e);
            w = bounds.x;
            h = bounds.y;
            break;
        }
        case Project::CanvasElementType::ColorRect:
        case Project::CanvasElementType::TextureRect:
        {
            auto wi = e.props.find("width");
            auto hi = e.props.find("height");
            if (wi != e.props.end()) w = propLitFloat(wi->second, w);
            if (hi != e.props.end()) h = propLitFloat(hi->second, h);
            break;
        }
        default: break;
    }

    w *= animScale;
    h *= animScale;

    auto tl = canvasToScreen({ex, ey}, origin);
    auto br = canvasToScreen({ex + w, ey + h}, origin);

    auto col = applyTint(elemColor(e.type));
    dl->AddRectFilled(tl, br, col);

    // Primary selection = bright outline; other multi-selected = dimmer; unselected = faint.
    bool isPrimary = (e.uuid == selectedUUID);
    bool inSel     = isSelected(e.uuid);
    ImU32 outline  = isPrimary ? COL_SEL_OUTLINE
                   : inSel     ? IM_COL32(255, 160, 40, 200)
                               : IM_COL32(200, 200, 200, 100);
    dl->AddRect(tl, br, outline, 0.f, 0, inSel ? 2.f : 1.f);

    if (e.type == Project::CanvasElementType::Text)
    {
        const char* preview = "[text]";
        auto txtIt = e.props.find("text");
        if (txtIt != e.props.end() && !txtIt->second.isBound &&
            txtIt->second.value.is_string())
            preview = txtIt->second.value.get_ref<const std::string&>().c_str();
        // Fixed 8px screen-space text so it doesn't scale with zoom
        float fontSize = 8.0f * zoom;
        dl->AddText(ImGui::GetFont(), fontSize, tl, applyTint(IM_COL32(220, 220, 220, 255)), preview);
    }

    for (const auto& child : e.children)
        drawElement(dl, origin, child, true, conf);
}

ImVec2 Editor::CanvasViewport::calcTextElementBounds(const Project::CanvasElement& e) const
{
    auto twi = e.props.find("tp_width");
    auto thi = e.props.find("tp_height");
    float setW = (twi != e.props.end()) ? propLitFloat(twi->second, 0.f) : 0.f;
    float setH = (thi != e.props.end()) ? propLitFloat(thi->second, 0.f) : 0.f;
    if (setW > 0.f && setH > 0.f)
        return {setW, setH};

    const char* preview = "[text]";
    auto txtIt = e.props.find("text");
    if (txtIt != e.props.end() && !txtIt->second.isBound &&
        txtIt->second.value.is_string())
        preview = txtIt->second.value.get_ref<const std::string&>().c_str();

    // Measure at 8px — at zoom=1 screen pixels == canvas pixels, so this gives
    // a stable canvas-space size that scales with the canvas like other elements.
    constexpr float FONT_PX = 8.0f;
    auto tsz = ImGui::GetFont()->CalcTextSizeA(FONT_PX, FLT_MAX, 0.0f, preview);
    float w = setW > 0.f ? setW : std::max(8.f, tsz.x);
    float h = setH > 0.f ? setH : std::max(8.f, FONT_PX);
    return {w, h};
}

Project::CanvasElement* Editor::CanvasViewport::findElement(
    std::vector<Project::CanvasElement>& elems, uint64_t uuid)
{
    for (auto& e : elems)
    {
        if (e.uuid == uuid) return &e;
        auto* found = findElement(e.children, uuid);
        if (found) return found;
    }
    return nullptr;
}

bool Editor::CanvasViewport::isSelected(uint64_t uuid) const
{
    return std::find(selection_.begin(), selection_.end(), uuid) != selection_.end();
}

// Base (un-animated) element rect in canvas space. Single source of truth shared by the
// hit-test, the draw, and alignment so they can't drift apart.
ImVec4 Editor::CanvasViewport::elementRect(const Project::Canvas& canvas,
                                           const Project::CanvasElement& e) const
{
    float ex = propLitFloat(e.x, 0.f);
    float ey = propLitFloat(e.y, 0.f);
    float w  = (float)canvas.conf.gridSizeX;
    float h  = (float)canvas.conf.gridSizeY;

    if (e.type == Project::CanvasElementType::Sprite)
    {
        auto spit = e.props.find("sprite");
        if (spit != e.props.end() && !spit->second.isBound &&
            spit->second.value.is_number_integer())
        {
            uint64_t sprUUID = spit->second.value.get<uint64_t>();
            if (sprUUID && ctx.project)
            {
                auto* entry = ctx.project->getAssets().getEntryByUUID(sprUUID);
                if (entry && entry->texture)
                {
                    w = (float)entry->texture->getWidth();
                    h = (float)entry->texture->getHeight();
                }
            }
        }
        auto sxi = e.props.find("blit_scale_x");
        auto syi = e.props.find("blit_scale_y");
        float scx = (sxi != e.props.end()) ? propLitFloat(sxi->second, 0.f) : 0.f;
        float scy = (syi != e.props.end()) ? propLitFloat(syi->second, 0.f) : 0.f;
        if (scx != 0.f) w *= scx;
        if (scy != 0.f) h *= scy;
    }
    else if (e.type == Project::CanvasElementType::Text)
    {
        auto bounds = calcTextElementBounds(e);
        w = bounds.x;
        h = bounds.y;
    }
    else
    {
        auto wi = e.props.find("width");
        auto hi = e.props.find("height");
        if (wi != e.props.end()) w = propLitFloat(wi->second, w);
        if (hi != e.props.end()) h = propLitFloat(hi->second, h);
    }
    return {ex, ey, w, h};
}

void Editor::CanvasViewport::handleInput(ImVec2 origin, Project::Canvas& canvas)
{
    auto& io = ImGui::GetIO();
    auto mousePos = io.MousePos;

    // Zoom with scroll wheel (zoom toward cursor)
    if (ImGui::IsItemHovered() && io.MouseWheel != 0.f)
    {
        float delta = io.MouseWheel * 0.15f;
        float newZoom = std::max(0.25f, std::min(zoom + delta, 8.f));
        ImVec2 before = screenToCanvas(mousePos, origin);
        zoom = newZoom;
        ImVec2 after = screenToCanvas(mousePos, origin);
        panOffset.x += (after.x - before.x) * zoom;
        panOffset.y += (after.y - before.y) * zoom;
    }

    // Pan with middle mouse button
    if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Middle))
        isPanning = true;
    if (isPanning)
    {
        panOffset.x += io.MouseDelta.x;
        panOffset.y += io.MouseDelta.y;
    }
    if (!ImGui::IsMouseDown(ImGuiMouseButton_Middle))
        isPanning = false;

    // Left click — select element (convert screen→canvas coordinates correctly)
    if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
    {
        auto cp = screenToCanvas(mousePos, origin);
        uint64_t hit = 0;
        for (auto& e : canvas.elements)        // last match wins (topmost is drawn last)
        {
            ImVec4 r = elementRect(canvas, e);
            if (cp.x >= r.x && cp.x <= r.x + r.z && cp.y >= r.y && cp.y <= r.y + r.w)
            {
                hit = e.uuid;
                dragStart = mousePos;
                isDragging = false;
            }
        }

        // Ctrl+click toggles an element in/out of the selection set (Shift stays reserved for
        // snap-drag). A plain click on an already-selected element keeps the set so the whole
        // group can be dragged; otherwise it single-selects.
        const bool additive = io.KeyCtrl;
        const uint64_t prevPrimary = selectedUUID;
        if (hit)
        {
            if (additive)
            {
                auto it = std::find(selection_.begin(), selection_.end(), hit);
                if (it != selection_.end()) {
                    selection_.erase(it);
                    if (selectedUUID == hit) selectedUUID = selection_.empty() ? 0 : selection_.back();
                } else {
                    selection_.push_back(hit);
                    selectedUUID = hit;
                }
            }
            else
            {
                if (!isSelected(hit)) { selection_.clear(); selection_.push_back(hit); }
                selectedUUID = hit;
            }
        }
        else if (!additive)
        {
            selection_.clear();
            selectedUUID = 0;
        }
        if (selectedUUID != prevPrimary) selectionChanged = true;
        dragStarts_.clear();   // captured fresh when a drag actually begins
    }

    // Drag the selected element(s) as a group.
    if (selectedUUID && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsItemActive())
    {
        if (!isDragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.f))
        {
            isDragging = true;
            dragStarts_.clear();
            for (uint64_t uid : selection_)
            {
                // Only drag selection "roots": if a selected ancestor exists, its shiftDescendants
                // already moves this element — dragging it again would double-move it.
                bool selectedAncestor = false;
                for (uint64_t other : selection_)
                    if (other != uid)
                        if (auto* oe = findElement(canvas.elements, other); oe && elementContains(*oe, uid))
                            { selectedAncestor = true; break; }
                if (selectedAncestor) continue;
                if (auto* el = findElement(canvas.elements, uid))
                    dragStarts_.push_back({uid, {propLitFloat(el->x, 0.f), propLitFloat(el->y, 0.f)}});
            }
        }

        if (isDragging)
        {
            ImVec2 delta{ (mousePos.x - dragStart.x) / zoom, (mousePos.y - dragStart.y) / zoom };
            float gx = (float)canvas.conf.gridSizeX;
            float gy = (float)canvas.conf.gridSizeY;
            for (auto& [uid, start] : dragStarts_)
            {
                auto* elem = findElement(canvas.elements, uid);
                if (!elem) continue;
                float newX = start.x + delta.x;
                float newY = start.y + delta.y;
                if (io.KeyShift) { newX = std::round(newX / gx) * gx; newY = std::round(newY / gy) * gy; }
                newX = std::round(newX);
                newY = std::round(newY);

                float curX = propLitFloat(elem->x, 0.f);
                float curY = propLitFloat(elem->y, 0.f);
                elem->x.isBound = false; elem->x.value = newX;
                elem->y.isBound = false; elem->y.value = newY;
                float dx = newX - curX, dy = newY - curY;
                if (dx != 0.f || dy != 0.f) shiftDescendants(elem->children, dx, dy);
            }
        }
    }
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        isDragging = false;
    }
}

void Editor::CanvasViewport::drawAlignBar(Project::Canvas& canvas)
{
    // This top row is ALWAYS drawn (even as a hint) so the canvas origin below it never jumps
    // when the toolbar appears/disappears.
    if (selection_.size() < 2) {
        ImGui::TextDisabled("Ctrl+click: multi-select  |  drag: move  |  Shift+drag: snap");
        return;
    }

    struct Sel { Project::CanvasElement* e; ImVec4 r; };
    std::vector<Sel> sel;
    for (uint64_t uid : selection_)
        if (auto* e = findElement(canvas.elements, uid))
            if (!e->x.isBound && !e->y.isBound)   // skip gameplay-positioned (bound) elements
                sel.push_back({e, elementRect(canvas, *e)});
    if (sel.size() < 2) {
        ImGui::TextDisabled("Align needs 2+ selected elements with literal x/y positions");
        return;
    }

    // Move an element to (x,y) (whole-pixel), shifting descendants — matches drag semantics.
    auto moveTo = [](Project::CanvasElement* e, float nx, float ny) {
        float cx = propLitFloat(e->x, 0.f), cy = propLitFloat(e->y, 0.f);
        nx = std::round(nx); ny = std::round(ny);
        e->x.isBound = false; e->x.value = nx;
        e->y.isBound = false; e->y.value = ny;
        if (nx != cx || ny != cy) shiftDescendants(e->children, nx - cx, ny - cy);
    };

    // Selection bounding box.
    float minX = sel[0].r.x, maxR = sel[0].r.x + sel[0].r.z;
    float minY = sel[0].r.y, maxB = sel[0].r.y + sel[0].r.w;
    for (auto& s : sel) {
        minX = std::min(minX, s.r.x); maxR = std::max(maxR, s.r.x + s.r.z);
        minY = std::min(minY, s.r.y); maxB = std::max(maxB, s.r.y + s.r.w);
    }
    float cX = (minX + maxR) * 0.5f, cY = (minY + maxB) * 0.5f;

    auto btn = [&](const char* lbl, const char* tip) {
        bool c = ImGui::SmallButton(lbl);
        if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", tip);
        ImGui::SameLine();
        return c;
    };

    bool changed = false;
    ImGui::TextUnformatted("Align:"); ImGui::SameLine();
    if (btn("L",  "Align left edges"))     { for (auto& s : sel) moveTo(s.e, minX, s.r.y); changed = true; }
    if (btn("cX", "Center horizontally"))  { for (auto& s : sel) moveTo(s.e, cX - s.r.z * 0.5f, s.r.y); changed = true; }
    if (btn("R",  "Align right edges"))    { for (auto& s : sel) moveTo(s.e, maxR - s.r.z, s.r.y); changed = true; }
    if (btn("T",  "Align top edges"))      { for (auto& s : sel) moveTo(s.e, s.r.x, minY); changed = true; }
    if (btn("cY", "Center vertically"))    { for (auto& s : sel) moveTo(s.e, s.r.x, cY - s.r.w * 0.5f); changed = true; }
    if (btn("B",  "Align bottom edges"))   { for (auto& s : sel) moveTo(s.e, s.r.x, maxB - s.r.w); changed = true; }

    if (sel.size() >= 3) {  // distribute spreads the middle elements' centers evenly
        if (btn("DH", "Distribute horizontally")) {
            std::sort(sel.begin(), sel.end(), [](const Sel& a, const Sel& b){ return a.r.x + a.r.z*0.5f < b.r.x + b.r.z*0.5f; });
            float c0 = sel.front().r.x + sel.front().r.z*0.5f, c1 = sel.back().r.x + sel.back().r.z*0.5f;
            float step = (c1 - c0) / (float)(sel.size() - 1);
            for (size_t i = 1; i + 1 < sel.size(); ++i) moveTo(sel[i].e, c0 + step*(float)i - sel[i].r.z*0.5f, sel[i].r.y);
            changed = true;
        }
        if (btn("DV", "Distribute vertically")) {
            std::sort(sel.begin(), sel.end(), [](const Sel& a, const Sel& b){ return a.r.y + a.r.w*0.5f < b.r.y + b.r.w*0.5f; });
            float c0 = sel.front().r.y + sel.front().r.w*0.5f, c1 = sel.back().r.y + sel.back().r.w*0.5f;
            float step = (c1 - c0) / (float)(sel.size() - 1);
            for (size_t i = 1; i + 1 < sel.size(); ++i) moveTo(sel[i].e, sel[i].r.x, c0 + step*(float)i - sel[i].r.w*0.5f);
            changed = true;
        }
    }
    ImGui::Text("(%zu selected)", sel.size());

    if (changed) Editor::CanvasHistory::markChanged("Align elements");
}

void Editor::CanvasViewport::draw(Project::Canvas& canvas)
{
    auto* dl = ImGui::GetWindowDrawList();
    auto windowSize = ImGui::GetWindowSize();

    // Multi-select alignment toolbar (top of the viewport; only shows when 2+ are selected).
    drawAlignBar(canvas);

    // Use the content area origin, not window pos (which includes title bar).
    // Capture it AFTER the optional toolbar so it's the true canvas top-left.
    ImVec2 origin = ImGui::GetCursorScreenPos();

    // Background fill — to the window's content bottom (origin may sit below the toolbar row, so
    // origin.y + windowSize.y would overshoot; clamp to the actual window bottom).
    float winBottom = ImGui::GetWindowPos().y + windowSize.y;
    dl->AddRectFilled(origin, {origin.x + windowSize.x, winBottom}, COL_CANVAS_BG);

    // Canvas boundary rect
    auto tl = canvasToScreen({0, 0}, origin);
    auto br = canvasToScreen({(float)canvas.conf.viewportWidth,
                               (float)canvas.conf.viewportHeight}, origin);
    dl->AddRectFilled(tl, br, IM_COL32(15, 15, 20, 255));
    dl->AddRect(tl, br, COL_CANVAS_BORD, 0.f, 0, 2.f);

    drawGrid(dl, origin, canvas.conf);

    for (const auto& e : canvas.elements)
        drawElement(dl, origin, e, true, canvas.conf);

    // InvisibleButton covers the full content area for input capture.
    // SetCursorScreenPos back to origin so it starts at the content top-left.
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("##canvasInput",
        {windowSize.x, windowSize.y - (origin.y - ImGui::GetWindowPos().y)},
        ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonMiddle);

    handleInput(origin, canvas);

    // Overlay hint text
    dl->AddText({origin.x + 4, origin.y + 4},
        IM_COL32(200, 200, 200, 100),
        ("Zoom: " + std::to_string((int)(zoom * 100)) + "% | Scroll=zoom | MMB=pan | Shift+drag=snap | Ctrl+click=multi-select").c_str());
}
