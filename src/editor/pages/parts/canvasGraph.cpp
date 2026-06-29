/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "canvasGraph.h"
#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "../../imgui/helper.h"
#include "../../canvasHistory.h"
#include "../../../utils/hash.h"
#include <algorithm>
#include <cctype>
#include <utility>   // std::move (libc++ needs it explicitly)

void Editor::CanvasGraph::regenUUIDs(Project::CanvasElement& e)
{
    e.uuid = Utils::Hash::randomU64();
    for (auto& child : e.children)
        regenUUIDs(child);
}

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
    e.rotation = lit(0.f);
    e.visible.value = true;

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

    // Find the vector + index that directly contains `uuid` (its parent's child list), for
    // in-place sibling duplication.
    bool findContainer(std::vector<Project::CanvasElement>& elems, uint64_t uuid,
                       std::vector<Project::CanvasElement>*& outVec, size_t& outIdx)
    {
        for (size_t i = 0; i < elems.size(); ++i) {
            if (elems[i].uuid == uuid) { outVec = &elems; outIdx = i; return true; }
            if (findContainer(elems[i].children, uuid, outVec, outIdx)) return true;
        }
        return false;
    }

    // True if `uuid` is `e` or any descendant of `e` — used to reject reparenting an element
    // into its own subtree (which would orphan it).
    bool inSubtree(const Project::CanvasElement& e, uint64_t uuid)
    {
        if (e.uuid == uuid) return true;
        for (const auto& c : e.children) if (inSubtree(c, uuid)) return true;
        return false;
    }

    bool ciContains(const std::string& hay, const std::string& needle)
    {
        if (needle.empty()) return true;
        auto eq = [](char a, char b){ return std::tolower((unsigned char)a) == std::tolower((unsigned char)b); };
        return std::search(hay.begin(), hay.end(), needle.begin(), needle.end(), eq) != hay.end();
    }

    // True if the element or any descendant name matches the filter (so ancestors of a match stay visible).
    bool subtreeMatches(const Project::CanvasElement& e, const std::string& f)
    {
        if (ciContains(e.name, f)) return true;
        for (const auto& c : e.children) if (subtreeMatches(c, f)) return true;
        return false;
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
    // Search/filter: hide elements whose subtree doesn't match.
    if (!filter_.empty() && !subtreeMatches(e, filter_)) return;

    ImGui::PushID((void*)(uintptr_t)e.uuid);

    // Eye-icon visibility toggle. Only literal visibility is editable here; a bound `visible`
    // is gameplay-driven, so the toggle is disabled and shows the authored default.
    bool bound = e.visible.isBound;
    bool vis   = bound || !e.visible.value.is_boolean() || e.visible.value.get<bool>();
    if (bound) ImGui::BeginDisabled();
    if (ImGui::SmallButton(vis ? "o" : "-")) {
        e.visible.value = !vis;
        Editor::CanvasHistory::markChanged("Toggle visibility");
    }
    if (bound) ImGui::EndDisabled();
    if (ImGui::IsItemHovered()) ImGui::SetTooltip(vis ? "Visible (click to hide)" : "Hidden (click to show)");
    ImGui::SameLine();

    ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow
        | ImGuiTreeNodeFlags_SpanAvailWidth;
    if (e.children.empty())
        flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
    if (e.uuid == selectedUUID)
        flags |= ImGuiTreeNodeFlags_Selected;

    // While filtering, force subtrees open so matches deep in the tree are visible.
    if (!filter_.empty()) ImGui::SetNextItemOpen(true, ImGuiCond_Always);

    if (!vis) ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(140, 140, 140, 255));
    bool open = ImGui::TreeNodeEx((void*)(uintptr_t)e.uuid, flags,
                                   "[%s] %s",
                                   ELEMENT_TYPE_NAMES[static_cast<int>(e.type)],
                                   e.name.c_str());
    if (!vis) ImGui::PopStyleColor();

    if (ImGui::IsItemClicked())
        selectedUUID = e.uuid;

    // Drag-to-reparent: drag a node onto another to make it that node's child (applied deferred).
    if (ImGui::BeginDragDropSource(ImGuiDragDropFlags_None)) {
        ImGui::SetDragDropPayload("CANVAS_ELEM", &e.uuid, sizeof(uint64_t));
        ImGui::Text("[%s] %s", ELEMENT_TYPE_NAMES[static_cast<int>(e.type)], e.name.c_str());
        ImGui::EndDragDropSource();
    }
    if (ImGui::BeginDragDropTarget()) {
        if (const ImGuiPayload* pl = ImGui::AcceptDragDropPayload("CANVAS_ELEM")) {
            reparentSrc_ = *static_cast<const uint64_t*>(pl->Data);
            reparentDst_ = e.uuid;
        }
        ImGui::EndDragDropTarget();
    }

    if (ImGui::BeginPopupContextItem())
    {
        selectedUUID = e.uuid;
        if (ImGui::MenuItem("Copy")) {
            clipboard_ = e;
        }
        if (ImGui::MenuItem("Duplicate", "Ctrl+D"))
            pendingDup_ = e.uuid;   // deferred: inserting here could realloc the vector mid-walk
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
    ImGui::PopID();
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

    // Search / filter box.
    ImGui::SetNextItemWidth(-ImGui::CalcTextSize("clear").x - ImGui::GetStyle().FramePadding.x * 4.f);
    ImGui::InputTextWithHint("##elemFilter", "search...", &filter_);
    if (!filter_.empty()) {
        ImGui::SameLine();
        if (ImGui::SmallButton("clear")) filter_.clear();
    }
    ImGui::Separator();

    // Hierarchy tree
    if (ImGui::BeginChild("##elemTree", ImVec2(0, -ImGui::GetFrameHeightWithSpacing())))
    {
        for (auto& e : canvas.elements)
            drawElement(canvas, e, 0);
        pruneDeleted(canvas.elements);

        // Keyboard: Ctrl+C copy, Ctrl+V paste, Ctrl+D duplicate (deferred), when not typing.
        if (!ImGui::GetIO().WantTextInput) {
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_C) && selectedUUID != 0) {
                auto* sel = getSelected(canvas);
                if (sel) clipboard_ = *sel;
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_V) && clipboard_.has_value()) {
                auto newElem = *clipboard_;
                regenUUIDs(newElem);
                newElem.name += "_copy";
                canvas.elements.push_back(std::move(newElem));
                Editor::CanvasHistory::markChanged("Paste element");
            }
            if (ImGui::GetIO().KeyCtrl && ImGui::IsKeyPressed(ImGuiKey_D) && selectedUUID != 0)
                pendingDup_ = selectedUUID;
        }

        // Apply a deferred duplicate now that traversal is done (safe to mutate the tree).
        if (pendingDup_ != 0) {
            std::vector<Project::CanvasElement>* vec = nullptr; size_t idx = 0;
            if (findContainer(canvas.elements, pendingDup_, vec, idx)) {
                auto dup = (*vec)[idx];
                regenUUIDs(dup);
                dup.name += "_copy";
                uint64_t newSel = dup.uuid;
                vec->insert(vec->begin() + (long)idx + 1, std::move(dup));
                selectedUUID = newSel;
                Editor::CanvasHistory::markChanged("Duplicate element");
            }
            pendingDup_ = 0;
        }

        // Apply a deferred drag-reparent (move src to become a child of dst), after traversal so
        // mutating the tree can't invalidate the walk. Reject moving an element into its own
        // subtree (would orphan it) or onto itself.
        if (reparentSrc_ != 0 && reparentSrc_ != reparentDst_) {
            auto* src = findByUUID(canvas.elements, reparentSrc_);
            if (src && !inSubtree(*src, reparentDst_)) {
                std::vector<Project::CanvasElement>* svec = nullptr; size_t sidx = 0;
                if (findContainer(canvas.elements, reparentSrc_, svec, sidx)) {
                    Project::CanvasElement moved = (*svec)[sidx];   // copy subtree before erase
                    svec->erase(svec->begin() + (long)sidx);
                    auto* dst = findByUUID(canvas.elements, reparentDst_);  // re-find post-erase
                    if (dst) {
                        dst->children.push_back(std::move(moved));
                        selectedUUID = reparentSrc_;
                        Editor::CanvasHistory::markChanged("Reparent element");
                    }
                }
            }
        }
        reparentSrc_ = reparentDst_ = 0;
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

    if (clipboard_.has_value()) {
        ImGui::SameLine();
        if (ImGui::Button("Paste")) {
            auto newElem = *clipboard_;
            regenUUIDs(newElem);
            newElem.name += "_copy";
            canvas.elements.push_back(std::move(newElem));
            Editor::CanvasHistory::markChanged("Paste element");
        }
    }
}
