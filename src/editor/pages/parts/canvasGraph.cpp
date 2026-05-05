/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "canvasGraph.h"
#include "imgui.h"
#include "../../imgui/helper.h"
#include "../../canvasHistory.h"
#include "../../../utils/hash.h"
#include <algorithm>

static constexpr const char* ELEMENT_TYPE_NAMES[] = {
    "Sprite", "Text", "ColorRect", "TextureRect"
};

void Editor::CanvasGraph::addDefaultProps(Project::CanvasElement& e)
{
    auto lit = [](float v) {
        Project::PropValue p;
        p.value = v;
        return p;
    };
    e.x = lit(0.f);
    e.y = lit(0.f);
    e.scaleX = lit(1.f);
    e.scaleY = lit(1.f);
    e.rotation = lit(0.f);

    switch (e.type)
    {
        case Project::CanvasElementType::Sprite:
            // "sprite" binding left unset — user binds via properties panel
            break;
        case Project::CanvasElementType::Text:
        {
            Project::PropValue tv;
            tv.value = std::string("Text");
            e.props["text"] = tv;
        } break;
        case Project::CanvasElementType::ColorRect:
        {
            Project::PropValue wv, hv, cv;
            wv.value = 32.f; e.props["width"] = wv;
            hv.value = 32.f; e.props["height"] = hv;
            cv.value = nlohmann::json::array({255, 255, 255, 255}); e.props["color"] = cv;
        } break;
        case Project::CanvasElementType::TextureRect:
        {
            Project::PropValue wv, hv;
            wv.value = 32.f; e.props["width"] = wv;
            hv.value = 32.f; e.props["height"] = hv;
        } break;
    }
}

namespace
{
    Project::CanvasElement* findByUUID(std::vector<Project::CanvasElement>& elements, uint64_t uuid)
    {
        for (auto& e : elements)
        {
            if (e.uuid == uuid) return &e;
            auto* found = findByUUID(e.children, uuid);
            if (found) return found;
        }
        return nullptr;
    }
}

Project::CanvasElement* Editor::CanvasGraph::getSelected(Project::Canvas& canvas)
{
    if (selectedUUID == 0) return nullptr;
    return findByUUID(canvas.elements, selectedUUID);
}

void Editor::CanvasGraph::drawElement(Project::Canvas& canvas,
                                       Project::CanvasElement& e, int /*depth*/)
{
    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (e.children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (e.uuid == selectedUUID)
        flags |= ImGuiTreeNodeFlags_Selected;

    bool open = ImGui::TreeNodeEx((void*)(uintptr_t)e.uuid, flags,
                                   "[%s] %s",
                                   ELEMENT_TYPE_NAMES[static_cast<int>(e.type)],
                                   e.name.c_str());

    if (ImGui::IsItemClicked())
        selectedUUID = e.uuid;

    if (ImGui::BeginPopupContextItem())
    {
        if (ImGui::MenuItem("Delete"))
        {
            e.uuid = 0; // sentinel for deletion (pruned after traversal)
            Editor::CanvasHistory::markChanged("Delete element");
        }
        ImGui::EndPopup();
    }

    if (open && !e.children.empty())
    {
        for (auto& child : e.children)
            drawElement(canvas, child, 0);
        ImGui::TreePop();
    }
}

static void pruneDeleted(std::vector<Project::CanvasElement>& elements)
{
    elements.erase(
        std::remove_if(elements.begin(), elements.end(),
            [](const Project::CanvasElement& e) { return e.uuid == 0; }),
        elements.end()
    );
    for (auto& e : elements)
        pruneDeleted(e.children);
}

void Editor::CanvasGraph::draw(Project::Canvas& canvas)
{
    ImGui::Text("Elements (%zu)", canvas.elements.size());
    ImGui::Separator();

    // Hierarchy tree
    if (ImGui::BeginChild("##elemTree", ImVec2(0, -ImGui::GetFrameHeightWithSpacing())))
    {
        for (auto& e : canvas.elements)
            drawElement(canvas, e, 0);
        pruneDeleted(canvas.elements);
    }
    ImGui::EndChild();

    // Add element menu
    if (ImGui::Button("+ Add Element"))
        ImGui::OpenPopup("##addElem");

    if (ImGui::BeginPopup("##addElem"))
    {
        for (int i = 0; i < 4; ++i)
        {
            if (ImGui::MenuItem(ELEMENT_TYPE_NAMES[i]))
            {
                Project::CanvasElement e;
                e.name = std::string(ELEMENT_TYPE_NAMES[i]) + "_new";
                e.type = static_cast<Project::CanvasElementType>(i);
                e.uuid = Utils::Hash::randomU64();
                addDefaultProps(e);
                canvas.elements.push_back(std::move(e));
                Editor::CanvasHistory::markChanged(std::string("Add ") + ELEMENT_TYPE_NAMES[i]);
            }
        }
        ImGui::EndPopup();
    }
}
