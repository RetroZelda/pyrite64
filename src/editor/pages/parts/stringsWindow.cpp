/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4.3).
#include "stringsWindow.h"

#include <algorithm>
#include <format>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "imgui.h"
#include "misc/cpp/imgui_stdlib.h"
#include "IconsMaterialDesignIcons.h"

#include "../../../context.h"
#include "../../../project/project.h"
#include "../../../project/locale/locProject.h"
#include "../../../project/locale/locMeasure.h"
#include "../../../utils/fs.h"
#include "../../imgui/notification.h"

namespace
{
  using Project::Loc::Status;

  enum class Filter : int { All, Missing, Review, Unused, Kept };
  const char* const FILTER_NAMES[] = {"All keys", "Missing text", "Needs review", "Unused", "Kept"};

  const ImVec4 COL_MISSING{1.0f, 0.45f, 0.40f, 1.0f};
  const ImVec4 COL_REVIEW{1.0f, 0.80f, 0.30f, 1.0f};
  const ImVec4 COL_UNUSED{0.55f, 0.55f, 0.58f, 1.0f};
  const ImVec4 COL_OK{0.55f, 0.85f, 0.55f, 1.0f};

  std::string oneLine(const std::string &text, size_t max = 60)
  {
    std::string out{};
    for(char c : text) out += (c == '\n') ? ' ' : c;
    if(out.size() > max) {
      // cut on a UTF-8 boundary
      size_t cut = max;
      while(cut > 0 && (static_cast<unsigned char>(out[cut]) & 0xC0) == 0x80) --cut;
      out = out.substr(0, cut) + "...";
    }
    return out;
  }

  const char* statusName(Status s)
  {
    switch(s) {
      case Status::Ok: return "ok";
      case Status::Missing: return "missing";
      case Status::MissingSource: return "no source text";
      case Status::NeedsReview: return "needs review";
    }
    return "";
  }
}

struct Editor::StringsWindow::State
{
  std::string projectPath{};
  Project::Loc::Uses uses{};
  Project::Loc::VarDecls vars{};
  bool scanned{false};
  std::vector<std::string> scanErrors{};

  Filter filter{Filter::All};
  std::string search{};
  std::string selected{};

  // the selected key's texts while they are edited, one per language
  std::string editKey{};
  std::vector<std::string> editText{};
  std::string editNote{};

  // create
  std::string newSourceCode{"en"};
  std::string newSourceName{"English"};

  // add key
  std::string addKey{};
  std::string addText{};

  std::vector<std::string> pendingRemove{};
  bool openRemove{false};
  bool openAdd{false};

  void scan(Project::Project &project)
  {
    scanErrors.clear();
    uses = Project::Loc::scanProject(project, scanErrors);
    vars = Project::Loc::scanVariables(project, scanErrors);
    scanned = true;
  }

  [[nodiscard]] size_t useCount(const std::string &key) const
  {
    auto it = uses.find(key);
    return it == uses.end() ? 0 : it->second.size();
  }
};

Editor::StringsWindow::StringsWindow() : state{std::make_unique<State>()} {}
Editor::StringsWindow::~StringsWindow() = default;

void Editor::StringsWindow::draw()
{
  auto &st = *state;
  if(!ctx.project) {
    ImGui::TextDisabled("No project is open");
    return;
  }
  auto &project = *ctx.project;

  if(st.projectPath != project.getPath()) {
    st = State{};
    st.projectPath = project.getPath();
  }

  // ---------------------------------------------------------- no localization yet
  if(!Project::Loc::hasLocalization(project.getPath())) {
    ImGui::TextWrapped("This project has no localization yet. Creating it makes a locale/ folder with "
                       "locale.json (the languages) and one string table for the source language.");
    ImGui::Spacing();
    ImGui::SetNextItemWidth(80);
    ImGui::InputText("Code", &st.newSourceCode);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(160);
    ImGui::InputText("Name", &st.newSourceName);
    if(ImGui::Button(ICON_MDI_TRANSLATE " Create locale/")) {
      if(Project::Loc::isValidKey(st.newSourceCode) && st.newSourceCode.find('.') == std::string::npos) {
        Project::Loc::create(project.getPath(), st.newSourceCode, st.newSourceName);
        Project::Loc::invalidateCache();
      } else {
        Editor::Noti::add(Editor::Noti::Type::ERROR, "A language code uses a-z, 0-9 and _, like \"en\"");
      }
    }
    return;
  }

  if(!st.scanned) st.scan(project);

  const auto &loc = Project::Loc::cached(project);
  const auto &cfg = loc.config;
  const auto *src = loc.source();
  const auto allKeys = loc.keys();

  // ---------------------------------------------------------- toolbar
  if(ImGui::Button(ICON_MDI_REFRESH " Refresh")) {
    auto res = Project::Loc::refreshCached(project);
    st.uses = res.uses;
    st.scanErrors = res.errors;
    st.vars = Project::Loc::scanVariables(project, st.scanErrors);
    st.scanned = true;
    Editor::Noti::add(Editor::Noti::Type::INFO, std::format("Strings: {} key(s) used, {} added, {} unused",
      res.uses.size(), res.added.size(), res.unused.size()));
  }
  ImGui::SetItemTooltip("Scan the project's C++, scenes and prefabs for keys, and add the missing ones to every language");
  ImGui::SameLine();

  std::vector<std::string> unusedKeys = loc.unused(st.uses);
  ImGui::BeginDisabled(unusedKeys.empty());
  if(ImGui::Button(std::format(ICON_MDI_DELETE_SWEEP " Remove unused ({})", unusedKeys.size()).c_str())) {
    st.pendingRemove = unusedKeys;
    st.openRemove = true;
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if(ImGui::Button(ICON_MDI_PLUS " Add key")) {
    st.addKey.clear();
    st.addText.clear();
    st.openAdd = true;
  }
  ImGui::SameLine();
  // spreadsheets for translators: locale/strings.csv
  const std::string csvPath = Project::Loc::localeDir(project.getPath()) + "/strings.csv";
  if(ImGui::Button(ICON_MDI_FILE_EXPORT " Export CSV")) {
    Utils::FS::saveTextFile(csvPath, Project::Loc::exportCsv(Project::Loc::cached(project)));
    Editor::Noti::add(Editor::Noti::Type::INFO, "Strings: wrote locale/strings.csv");
  }
  ImGui::SetItemTooltip("Every key, its note and its text in each language, for a spreadsheet: locale/strings.csv");
  ImGui::SameLine();
  if(ImGui::Button(ICON_MDI_FILE_IMPORT " Import CSV")) {
    std::vector<std::string> csvErrors{};
    const int changed = Project::Loc::importCsv(Project::Loc::edit(project), Utils::FS::loadTextFile(csvPath), csvErrors);
    Project::Loc::saveCached(project);
    st.editKey.clear();
    Editor::Noti::add(csvErrors.empty() ? Editor::Noti::Type::INFO : Editor::Noti::Type::ERROR,
      std::format("Strings: {} text(s) changed from locale/strings.csv{}", changed, csvErrors.empty() ? "" : ", " + csvErrors[0]));
  }
  ImGui::SetItemTooltip("Read the translations back from locale/strings.csv");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(150);
  int filterIdx = static_cast<int>(st.filter);
  if(ImGui::Combo("##filter", &filterIdx, FILTER_NAMES, IM_ARRAYSIZE(FILTER_NAMES))) st.filter = static_cast<Filter>(filterIdx);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(220);
  ImGui::InputTextWithHint("##search", ICON_MDI_MAGNIFY " key or text", &st.search);

  const auto &loadErrors = Project::Loc::cachedErrors(project);
  const size_t errorCount = loadErrors.size() + st.scanErrors.size();
  if(errorCount > 0) {
    ImGui::SameLine();
    ImGui::TextColored(COL_MISSING, ICON_MDI_ALERT " %zu problem(s)", errorCount);
    if(ImGui::BeginItemTooltip()) {
      for(const auto &e : loadErrors) ImGui::TextUnformatted(e.c_str());
      for(const auto &e : st.scanErrors) ImGui::TextUnformatted(e.c_str());
      ImGui::EndTooltip();
    }
  }

  // ---------------------------------------------------------- rows
  std::vector<std::string> rows{};
  for(const auto &key : allKeys) {
    if(!st.search.empty()) {
      bool hit = key.find(st.search) != std::string::npos;
      for(size_t li = 0; !hit && li < loc.tables.size(); ++li) {
        auto it = loc.tables[li].entries.find(key);
        if(it != loc.tables[li].entries.end() && it->second.text.find(st.search) != std::string::npos) hit = true;
      }
      if(!hit) continue;
    }
    bool keep = true;
    switch(st.filter) {
      case Filter::All: break;
      case Filter::Missing:
        keep = false;
        for(size_t li = 0; li < cfg.languages.size(); ++li) {
          auto s = loc.status(key, li);
          if(s == Status::Missing || s == Status::MissingSource) { keep = true; break; }
        }
        break;
      case Filter::Review:
        keep = false;
        for(size_t li = 0; li < cfg.languages.size(); ++li) {
          if(loc.status(key, li) == Status::NeedsReview) { keep = true; break; }
        }
        break;
      case Filter::Unused: keep = st.useCount(key) == 0 && !loc.isKept(key); break;
      case Filter::Kept: keep = loc.isKept(key); break;
    }
    if(keep) rows.push_back(key);
  }

  ImGui::TextDisabled("%zu key(s), %zu shown, languages:", allKeys.size(), rows.size());
  for(const auto &lang : cfg.languages) {
    ImGui::SameLine();
    const std::string pair = cfg.pairOf(lang.code);
    ImGui::TextDisabled("%s%s%s%s", lang.code.c_str(), lang.code == cfg.source ? " (source)" : "",
      lang.pseudo ? " (pseudo)" : "", pair.empty() ? "" : (" <> " + pair).c_str());
  }

  const float detailHeight = st.selected.empty() ? 0.0f : std::max(160.0f, ImGui::GetContentRegionAvail().y * 0.4f);
  const int columns = 2 + static_cast<int>(cfg.languages.size());
  const ImGuiTableFlags flags = ImGuiTableFlags_ScrollY | ImGuiTableFlags_ScrollX | ImGuiTableFlags_RowBg
    | ImGuiTableFlags_BordersInnerV | ImGuiTableFlags_Resizable | ImGuiTableFlags_SizingFixedFit;
  if(ImGui::BeginTable("##strings", columns, flags, ImVec2(0, -detailHeight))) {
    ImGui::TableSetupScrollFreeze(1, 1);
    ImGui::TableSetupColumn("Key", ImGuiTableColumnFlags_WidthFixed, 220);
    for(const auto &lang : cfg.languages) {
      ImGui::TableSetupColumn(lang.code.c_str(), ImGuiTableColumnFlags_WidthFixed, lang.pseudo ? 180.0f : 260.0f);
    }
    ImGui::TableSetupColumn("Used", ImGuiTableColumnFlags_WidthFixed, 50);
    ImGui::TableHeadersRow();

    ImGuiListClipper clipper{};
    clipper.Begin(static_cast<int>(rows.size()));
    while(clipper.Step()) {
      for(int r = clipper.DisplayStart; r < clipper.DisplayEnd; ++r) {
        const auto &key = rows[r];
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const bool unused = st.useCount(key) == 0 && !loc.isKept(key);
        if(unused) ImGui::PushStyleColor(ImGuiCol_Text, COL_UNUSED);
        if(ImGui::Selectable(key.c_str(), st.selected == key, ImGuiSelectableFlags_SpanAllColumns | ImGuiSelectableFlags_AllowOverlap)) {
          st.selected = key;
        }
        if(unused) ImGui::PopStyleColor();

        for(size_t li = 0; li < cfg.languages.size(); ++li) {
          ImGui::TableNextColumn();
          const auto &lang = cfg.languages[li];
          if(lang.pseudo) {
            std::string text{};
            if(src) {
              auto it = src->entries.find(key);
              if(it != src->entries.end()) text = Project::Loc::pseudoText(it->second.text).substr(1); // shown without the escape
            }
            ImGui::TextDisabled("%s", oneLine(text, 40).c_str());
            continue;
          }
          const auto status = loc.status(key, li);
          auto it = loc.tables[li].entries.find(key);
          const std::string text = it == loc.tables[li].entries.end() ? std::string{} : it->second.text;
          if(status == Status::Missing || status == Status::MissingSource) {
            ImGui::TextColored(COL_MISSING, "%s", text.empty() ? "(missing)" : oneLine(text).c_str());
          } else if(status == Status::NeedsReview) {
            ImGui::TextColored(COL_REVIEW, ICON_MDI_ALERT_CIRCLE_OUTLINE " %s", oneLine(text).c_str());
          } else {
            ImGui::TextUnformatted(oneLine(text).c_str());
          }
        }

        ImGui::TableNextColumn();
        const size_t used = st.useCount(key);
        if(loc.isKept(key)) ImGui::TextDisabled(ICON_MDI_PIN " %zu", used);
        else if(used == 0) ImGui::TextColored(COL_UNUSED, "0");
        else ImGui::Text("%zu", used);
      }
    }
    ImGui::EndTable();
  }

  // ---------------------------------------------------------- the selected key
  if(!st.selected.empty() && allKeys.contains(st.selected)) {
    auto &edit = Project::Loc::edit(project);
    const auto &key = st.selected;

    // load the edit buffers when the selection changes, or when the files changed under them
    if(st.editKey != key || st.editText.size() != edit.tables.size()) {
      st.editKey = key;
      st.editText.assign(edit.tables.size(), {});
      for(size_t li = 0; li < edit.tables.size(); ++li) {
        auto it = edit.tables[li].entries.find(key);
        if(it != edit.tables[li].entries.end()) st.editText[li] = it->second.text;
      }
      st.editNote.clear();
      if(auto *s = edit.source()) {
        auto it = s->entries.find(key);
        if(it != s->entries.end()) st.editNote = it->second.note;
      }
    }

    ImGui::Separator();
    ImGui::BeginChild("##detail", ImVec2(0, 0), ImGuiChildFlags_None);
    ImGui::Text(ICON_MDI_KEY " %s", key.c_str());
    ImGui::SameLine();
    const int srcIdx = edit.sourceIndex();
    if(srcIdx >= 0) {
      auto &srcEntry = edit.tables[srcIdx].entries[key];
      bool keepFlag = srcEntry.keep;
      if(ImGui::Checkbox("Keep", &keepFlag)) {
        srcEntry.keep = keepFlag;
        Project::Loc::saveCached(project);
      }
      ImGui::SetItemTooltip("Never list this key as unused (for keys that code builds at run time)");
    }

    ImGui::SetNextItemWidth(-1);
    ImGui::InputTextWithHint("##note", "a note for translators: who speaks, a length limit, context", &st.editNote);
    if(ImGui::IsItemDeactivatedAfterEdit() && srcIdx >= 0) {
      edit.tables[srcIdx].entries[key].note = st.editNote;
      Project::Loc::saveCached(project);
    }

    if(ImGui::BeginTable("##langs", 2, ImGuiTableFlags_SizingStretchProp)) {
      ImGui::TableSetupColumn("lang", ImGuiTableColumnFlags_WidthFixed, 140);
      ImGui::TableSetupColumn("text", ImGuiTableColumnFlags_WidthStretch);
      for(size_t li = 0; li < edit.config.languages.size() && li < st.editText.size(); ++li) {
        const auto &lang = edit.config.languages[li];
        if(lang.pseudo) continue;
        ImGui::TableNextRow();
        ImGui::TableNextColumn();
        const auto status = edit.status(key, li);
        ImGui::Text("%s", lang.name.c_str());
        const ImVec4 col = status == Status::Ok ? COL_OK : status == Status::NeedsReview ? COL_REVIEW : COL_MISSING;
        ImGui::TextColored(col, "%s", statusName(status));
        if(status == Status::NeedsReview) {
          ImGui::PushID(static_cast<int>(li));
          if(ImGui::SmallButton(ICON_MDI_CHECK " Still right")) {
            edit.confirm(key, li);
            Project::Loc::saveCached(project);
          }
          ImGui::SetItemTooltip("The source text changed since this was translated. Mark the translation as checked.");
          ImGui::PopID();
        }
        ImGui::TableNextColumn();
        ImGui::PushID(static_cast<int>(li) + 1000);
        ImGui::InputTextMultiline("##text", &st.editText[li], ImVec2(-1, ImGui::GetTextLineHeight() * 3.2f));
        if(ImGui::IsItemDeactivatedAfterEdit()) {
          edit.setText(key, li, st.editText[li]);
          Project::Loc::saveCached(project);
        }
        // the build's checks, while typing: unknown tags, undeclared variables, plural cases
        if(!st.editText[li].empty()) {
          std::vector<std::string> envErrors{};
          auto env = Project::Loc::makeCompileEnv(edit.config, st.vars, envErrors);
          std::vector<std::string> textErrors{};
          Project::Loc::compileText(st.editText[li], env, Project::Loc::pluralRuleOf(lang), textErrors);
          for(const auto &err : textErrors) ImGui::TextColored(COL_MISSING, ICON_MDI_ALERT " %s", err.c_str());

          // dialog lines: about how many pages of the talk box, longer than the source's is flagged
          if(key.starts_with("dlg.")) {
            const auto box = Project::Loc::boxSizeOf(edit.config);
            const auto dir = Project::Loc::localeDir(project.getPath());
            const auto here = Project::Loc::estimatePages(dir, env, lang, st.editText[li], box);
            std::optional<Project::Loc::PageEstimate> base{};
            const int si = edit.sourceIndex();
            if(si >= 0 && static_cast<size_t>(si) != li && static_cast<size_t>(si) < st.editText.size()) {
              base = Project::Loc::estimatePages(dir, env, edit.config.languages[size_t(si)], st.editText[size_t(si)], box);
            }
            if(here) {
              const bool longer = base && here->pages > base->pages;
              ImGui::TextColored(longer ? COL_REVIEW : COL_UNUSED, "%s %d line(s), %d page(s) of the talk box%s",
                longer ? ICON_MDI_ALERT : ICON_MDI_RULER, here->lines, here->pages, longer ? ", more than the source" : "");
            }
          }
        }
        ImGui::PopID();
      }
      ImGui::EndTable();
    }

    auto it = st.uses.find(key);
    if(it == st.uses.end() || it->second.empty()) {
      ImGui::TextColored(COL_UNUSED, loc.isKept(key) ? "Nothing uses this key; it is kept" : "Nothing uses this key");
    } else {
      ImGui::TextDisabled("Used by:");
      for(const auto &use : it->second) {
        if(use.file.empty()) ImGui::BulletText("%s", use.where.c_str());
        else ImGui::BulletText("%s:%d", use.file.c_str(), use.line);
      }
    }
    ImGui::EndChild();
  }

  // ---------------------------------------------------------- remove unused (confirm)
  if(st.openRemove) { ImGui::OpenPopup("Remove unused strings"); st.openRemove = false; }
  ImGui::SetNextWindowSize(ImVec2(560, 0), ImGuiCond_Appearing);
  if(ImGui::BeginPopupModal("Remove unused strings", nullptr)) {
    ImGui::TextWrapped("Nothing in the project's C++, scenes or prefabs uses these %zu key(s). Removing them "
                       "deletes them from every language, translations included. Mark a key Keep to protect it.",
                       st.pendingRemove.size());
    ImGui::BeginChild("##list", ImVec2(0, 240), ImGuiChildFlags_Borders);
    for(const auto &key : st.pendingRemove) {
      std::string text{};
      if(src) {
        auto e = src->entries.find(key);
        if(e != src->entries.end()) text = e->second.text;
      }
      ImGui::BulletText("%s", key.c_str());
      ImGui::SameLine();
      ImGui::TextDisabled("%s", oneLine(text, 50).c_str());
    }
    ImGui::EndChild();
    if(ImGui::Button(std::format(ICON_MDI_DELETE " Remove {} key(s) from every language", st.pendingRemove.size()).c_str())) {
      Project::Loc::edit(project).remove(st.pendingRemove);
      Project::Loc::saveCached(project);
      if(std::find(st.pendingRemove.begin(), st.pendingRemove.end(), st.selected) != st.pendingRemove.end()) st.selected.clear();
      st.pendingRemove.clear();
      st.editKey.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(ImGui::Button("Cancel")) {
      st.pendingRemove.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  // ---------------------------------------------------------- add a key
  if(st.openAdd) { ImGui::OpenPopup("Add a key"); st.openAdd = false; }
  ImGui::SetNextWindowSize(ImVec2(480, 0), ImGuiCond_Appearing);
  if(ImGui::BeginPopupModal("Add a key", nullptr)) {
    ImGui::SetNextItemWidth(-1);
    if(ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere();
    ImGui::InputTextWithHint("##key", "npc.tank.greet", &st.addKey);
    std::string why{};
    const bool valid = Project::Loc::isValidKey(st.addKey, &why);
    const bool exists = allKeys.contains(st.addKey);
    if(!st.addKey.empty() && !valid) ImGui::TextColored(COL_MISSING, "%s", why.c_str());
    else if(exists) ImGui::TextColored(COL_REVIEW, "this key exists already");
    ImGui::InputTextMultiline("##text", &st.addText, ImVec2(-1, ImGui::GetTextLineHeight() * 3.2f));
    ImGui::TextDisabled("Text in %s. Use it in C++ as \"%s\"_localized", cfg.source.c_str(), st.addKey.empty() ? "key" : st.addKey.c_str());
    ImGui::BeginDisabled(!valid || exists);
    if(ImGui::Button("Add")) {
      auto &edit = Project::Loc::edit(project);
      Project::Loc::Uses one{};
      one[st.addKey].push_back({"added by hand", {}, 0});
      edit.addMissing(one);
      if(edit.sourceIndex() >= 0) edit.setText(st.addKey, static_cast<size_t>(edit.sourceIndex()), st.addText);
      Project::Loc::saveCached(project);
      st.selected = st.addKey;
      st.editKey.clear();
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if(ImGui::Button("Cancel")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }
}
