/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "canvasViewport.h"
#include "imgui.h"
#include "imgui_internal.h"
#include <algorithm>
#include <cmath>

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
                                          const Project::CanvasElement& e, bool parentVisible)
{
    if (!e.visible || !parentVisible) return;

    float ex = propLitFloat(e.x, 0.f);
    float ey = propLitFloat(e.y, 0.f);
    float scx = propLitFloat(e.scaleX, 1.f);
    float scy = propLitFloat(e.scaleY, 1.f);

    float w = 32.f * scx;
    float h = 32.f * scy;

    if (e.type == Project::CanvasElementType::Text)
    { w = 64.f * scx; h = 16.f * scy; }
    else if (e.type == Project::CanvasElementType::ColorRect ||
             e.type == Project::CanvasElementType::TextureRect)
    {
        auto wi = e.props.find("width");
        auto hi = e.props.find("height");
        if (wi != e.props.end()) w = propLitFloat(wi->second, 32.f) * scx;
        if (hi != e.props.end()) h = propLitFloat(hi->second, 32.f) * scy;
    }

    auto tl = canvasToScreen({ex, ey}, origin);
    auto br = canvasToScreen({ex + w, ey + h}, origin);

    auto col = elemColor(e.type);
    dl->AddRectFilled(tl, br, col);

    bool selected = (e.uuid == selectedUUID);
    dl->AddRect(tl, br, selected ? COL_SEL_OUTLINE : IM_COL32(200, 200, 200, 100),
                0.f, 0, selected ? 2.f : 1.f);

    if (e.type == Project::CanvasElementType::Text)
    {
        auto txtIt = e.props.find("text");
        const char* preview = "[text]";
        if (txtIt != e.props.end() && !txtIt->second.isBound &&
            txtIt->second.value.is_string())
        {
            preview = txtIt->second.value.get_ref<const std::string&>().c_str();
        }
        dl->AddText(tl, IM_COL32(220, 220, 220, 255), preview);
    }

    for (const auto& child : e.children)
        drawElement(dl, origin, child, true);
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
        for (auto& e : canvas.elements)
        {
            float ex = propLitFloat(e.x, 0.f);
            float ey = propLitFloat(e.y, 0.f);
            float scx = propLitFloat(e.scaleX, 1.f);
            float scy = propLitFloat(e.scaleY, 1.f);
            float w = 32.f * scx, h = 32.f * scy;

            auto wi = e.props.find("width");
            auto hi = e.props.find("height");
            if (wi != e.props.end()) w = propLitFloat(wi->second, 32.f) * scx;
            if (hi != e.props.end()) h = propLitFloat(hi->second, 32.f) * scy;

            if (cp.x >= ex && cp.x <= ex + w && cp.y >= ey && cp.y <= ey + h)
            {
                hit = e.uuid;
                dragStart = mousePos;
                dragObjStart = {ex, ey};
                isDragging = false;
            }
        }
        if (selectedUUID != hit)
            selectionChanged = true;
        selectedUUID = hit;
    }

    // Drag selected element
    if (selectedUUID && ImGui::IsMouseDown(ImGuiMouseButton_Left) && ImGui::IsItemActive())
    {
        if (!isDragging && ImGui::IsMouseDragging(ImGuiMouseButton_Left, 2.f))
            isDragging = true;

        if (isDragging)
        {
            auto delta = ImVec2{mousePos.x - dragStart.x, mousePos.y - dragStart.y};
            auto* elem = findElement(canvas.elements, selectedUUID);
            if (elem)
            {
                float newX = dragObjStart.x + delta.x / zoom;
                float newY = dragObjStart.y + delta.y / zoom;
                if (io.KeyShift)
                {
                    float gx = (float)canvas.conf.gridSizeX;
                    float gy = (float)canvas.conf.gridSizeY;
                    newX = std::round(newX / gx) * gx;
                    newY = std::round(newY / gy) * gy;
                }
                // Always snap to whole-pixel positions
                newX = std::round(newX);
                newY = std::round(newY);
                elem->x.isBound = false;
                elem->x.value = newX;
                elem->y.isBound = false;
                elem->y.value = newY;
            }
        }
    }
    else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left))
    {
        isDragging = false;
    }
}

void Editor::CanvasViewport::draw(Project::Canvas& canvas)
{
    auto* dl = ImGui::GetWindowDrawList();
    auto windowSize = ImGui::GetWindowSize();

    // Use the content area origin, not window pos (which includes title bar).
    // Capture it BEFORE drawing anything so it's the true content top-left.
    ImVec2 origin = ImGui::GetCursorScreenPos();

    // Background fill
    dl->AddRectFilled(origin,
        {origin.x + windowSize.x, origin.y + windowSize.y},
        COL_CANVAS_BG);

    // Canvas boundary rect
    auto tl = canvasToScreen({0, 0}, origin);
    auto br = canvasToScreen({(float)canvas.conf.viewportWidth,
                               (float)canvas.conf.viewportHeight}, origin);
    dl->AddRectFilled(tl, br, IM_COL32(15, 15, 20, 255));
    dl->AddRect(tl, br, COL_CANVAS_BORD, 0.f, 0, 2.f);

    drawGrid(dl, origin, canvas.conf);

    for (const auto& e : canvas.elements)
        drawElement(dl, origin, e, true);

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
        ("Zoom: " + std::to_string((int)(zoom * 100)) + "% | Scroll=zoom | MMB=pan | Shift+drag=snap").c_str());
}
