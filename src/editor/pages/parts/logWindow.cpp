/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#include "logWindow.h"

#include "imgui.h"
#include "../../../context.h"
#include "../../../utils/logger.h"
#include "../../imgui/theme.h"
#include "imgui_internal.h"
#include "../../../utils/fs.h"
#include "../../../utils/time.h"
#include "../../imgui/notification.h"
#include <algorithm>
#include <cctype>

namespace
{
  constinit uint32_t lastLen{0};

  constexpr ImVec4 COLOR_WARNING{1.0f, 0.85f, 0.0f, 1.0f};
  constexpr ImVec4 COLOR_ERROR{1.0f, 0.35f, 0.35f, 1.0f};

  void renderBuildLine(const std::string &line)
  {
    std::string lower = line;
    std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c){ return std::tolower(c); });

    struct Seg { size_t start, end; const ImVec4 *color; };
    std::vector<Seg> segs;

    for (size_t p = 0; (p = lower.find("warning", p)) != std::string::npos; p += 7)
      segs.push_back({p, p + 7, &COLOR_WARNING});
    for (size_t p = 0; (p = lower.find("error", p)) != std::string::npos; p += 5)
      segs.push_back({p, p + 5, &COLOR_ERROR});

    if (segs.empty()) {
      ImGui::TextUnformatted(line.c_str(), line.c_str() + line.size());
      return;
    }

    std::sort(segs.begin(), segs.end(), [](const Seg &a, const Seg &b){ return a.start < b.start; });

    size_t cur = 0;
    bool first = true;
    for (const auto &s : segs) {
      if (s.start > cur) {
        if (!first) ImGui::SameLine(0, 0);
        ImGui::TextUnformatted(line.c_str() + cur, line.c_str() + s.start);
        first = false;
      }
      if (!first) ImGui::SameLine(0, 0);
      ImGui::PushStyleColor(ImGuiCol_Text, *s.color);
      ImGui::TextUnformatted(line.c_str() + s.start, line.c_str() + s.end);
      ImGui::PopStyleColor();
      first = false;
      cur = s.end;
    }
    if (cur < line.size()) {
      ImGui::SameLine(0, 0);
      ImGui::TextUnformatted(line.c_str() + cur, line.c_str() + line.size());
    }
  }
}

void Editor::LogWindow::draw()
{
  auto log = Utils::Logger::getLog();

  //ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(4, 4));
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0)); // Remove space between children

  ImGui::BeginChild("TOP", ImVec2(0, 22_px),
    ImGuiChildFlags_AlwaysUseWindowPadding,
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
  );

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4_px, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    float btnWidth = 64_px;
    if(ImGui::Button("Clear", {btnWidth, 0})) {
      Utils::Logger::clear();
    }

    ImGui::SameLine();
    if(ImGui::Button("Copy", {btnWidth, 0})) {
      ImGui::SetClipboardText(log.c_str());
    }

    ImGui::SameLine();
    if(ImGui::Button("Save", {btnWidth, 0}))
    {
      std::string dateTimeStr = Utils::Time::getDateTimeStr();
      auto path = fs::path{ctx.project->getPath()} / ("log_" + dateTimeStr + ".txt");
      if(Utils::FS::saveTextFile(path, log)) {
        Editor::Noti::add(Noti::Type::SUCCESS, "Log saved to: " + path.string());
      } else {
        Editor::Noti::add(Noti::Type::ERROR, "Failed to save log to: " + path.string());
      }
    }

    ImGui::PopStyleVar(2);

  ImGui::EndChild();

  ImGui::BeginChild("LOG", ImVec2(0, 0), ImGuiChildFlags_Borders);
  ImGui::PushFont(ImGui::Theme::getFontMono(), 16_px);

  ImGui::PushID("LOG");
  ImGui::PushStyleColor(ImGuiCol_FrameBg, {0.05f, 0.05f, 0.06f, 1.0f});

  const char* child_window_name = NULL;
  ImFormatStringToTempBuffer(&child_window_name, NULL, "Log/LOG_43838E2B/%08X", ImGui::GetID(""));

  auto &logStripped = Utils::Logger::getLogStripped();
  ImGui::InputTextMultiline("", (char*)logStripped.data(), logStripped.size()+1, {
    ImGui::GetWindowSize().x-6,
    ImGui::GetWindowSize().y-6
  }, ImGuiInputTextFlags_ReadOnly);


  if (lastLen != logStripped.length()) {
    lastLen = logStripped.length();
    if(auto *childWindow = ImGui::FindWindowByName(child_window_name)) {
      ImGui::SetScrollY(childWindow, childWindow->ScrollMax.y);
    }
  }

  ImGui::PopStyleColor();
  ImGui::PopID();

  ImGui::PopFont();
  ImGui::EndChild();

  ImGui::PopStyleVar(2);
}

void Editor::LogWindow::drawBuild()
{
  const auto &lines = Utils::Logger::getBuildLines();

  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, 0));

  ImGui::BeginChild("TOP_BUILD", ImVec2(0, 22_px),
    ImGuiChildFlags_AlwaysUseWindowPadding,
    ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse
  );

    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4_px, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    float btnWidth = 64_px;
    if (ImGui::Button("Clear", {btnWidth, 0})) {
      Utils::Logger::clearBuild();
    }
    ImGui::SameLine();
    if (ImGui::Button("Copy", {btnWidth, 0})) {
      std::string all;
      for (const auto &l : lines) { all += l; all += '\n'; }
      ImGui::SetClipboardText(all.c_str());
    }
    ImGui::PopStyleVar(2);

  ImGui::EndChild();

  ImGui::BeginChild("BUILD_LOG", ImVec2(0, 0), ImGuiChildFlags_Borders);
  ImGui::PushFont(ImGui::Theme::getFontMono(), 16_px);

  ImGuiListClipper clipper;
  clipper.Begin((int)lines.size());
  while (clipper.Step()) {
    for (int i = clipper.DisplayStart; i < clipper.DisplayEnd; i++) {
      renderBuildLine(lines[i]);
    }
  }
  clipper.End();

  if (lastBuildLineCount != (uint32_t)lines.size()) {
    lastBuildLineCount = (uint32_t)lines.size();
    ImGui::SetScrollY(ImGui::GetScrollMaxY());
  }

  ImGui::PopFont();
  ImGui::EndChild();

  ImGui::PopStyleVar(2);
}
