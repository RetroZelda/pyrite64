/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: dialog trees (docs/todo/dialog/PLAN.md, section 7.1). The editor of one .p64dlg: lines,
// choices, conditions and events as nodes, the lines' text edited in place in the preview
// language (it goes straight into locale/), and Play, which walks the tree in the editor.
#pragma once
#include <cstdint>
#include <memory>
#include <string>

namespace Editor
{
  class DialogTreeWindow
  {
    public:
      explicit DialogTreeWindow(uint64_t assetUUID);
      ~DialogTreeWindow();

      /** Draws the window; false once it is closed. */
      bool draw();
      void save();

      [[nodiscard]] bool isDirty() const;
      [[nodiscard]] uint64_t getAssetUUID() const;
      [[nodiscard]] const std::string &getName() const;

      struct State; // public so the window's node class can reach it

    private:
      std::unique_ptr<State> state;
  };
}
