/**
* @copyright 2025 - Max Bebök
* @license MIT
*/
#pragma once
#include <array>
#include <string>

#include "../../../renderer/texture.h"

namespace Editor
{
  class AssetsBrowser
  {
    private:
      int activeTab{1};
      int prevActiveTab{-1};
      std::array<std::string, 5> tabDirs{};
      std::string searchFilter{};
      std::string renamePath{};
      std::string deletePath{};
      char renameBuffer[256];

    public:
      static constexpr int TAB_SCENES  = 0;
      static constexpr int TAB_ASSETS  = 1;
      static constexpr int TAB_CANVAS  = 4;

      int getActiveTab() const { return activeTab; }
      // Sets the active tab without triggering the change-detection event next frame.
      void forceTab(int t) { activeTab = t; prevActiveTab = t; }
      // Sets the active tab and allows change-detection to fire next frame.
      void setActiveTab(int t) { activeTab = t; }
      void draw();
      void showContextMenu(const std::string& path);
  };
}