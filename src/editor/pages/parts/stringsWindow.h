/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: localization (docs/todo/dialog/PLAN.md, section 4.3). The Strings window: every key of
// the project's string tables, its text in each language and its status, with Refresh (add the
// keys the project uses) and Remove unused (after a confirmation).
#pragma once
#include <memory>

namespace Editor
{
  class StringsWindow
  {
    public:
      StringsWindow();
      ~StringsWindow();

      /** Draws the window's content; call between ImGui::Begin("Strings") and End(). */
      void draw();

    private:
      struct State;
      std::unique_ptr<State> state;
  };
}
