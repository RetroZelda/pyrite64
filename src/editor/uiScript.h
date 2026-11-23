/**
* @copyright 2026 - Full-Frontal fork
* @license MIT
*/
// Fork: plays a file of mouse and keyboard steps into the editor (--ui-script), through SDL's own
// event queue, for screenshots, videos and tests of the UI without anyone at the machine.
//
//   wait 30               frames to wait
//   move 400 360 [steps]  pointer to window pixel x, y (over 'steps' frames)
//   down / up             left button; rdown / rup: right button
//   click 400 360         move, press and release
//   rclick 400 360        the same with the right button
//   key Delete            press and release a key (Delete, Backspace, Return, Escape, Tab, A-Z)
//   wheel -3 [x]          scroll at the pointer (negative: down), optional horizontal amount
//   type text             text input
//   mark name             prints "[ui] mark name" on stdout, for a capture by the caller
//   save                  saves the project (like Ctrl+S)
//   quit                  closes the editor
// Lines starting with # are comments.
#pragma once
#include <string>

struct SDL_Window;

namespace Editor::UiScript
{
  /** Loads a script; false if it could not be read. */
  bool load(const std::string &path);

  /** Pushes this frame's events. Call once per frame before polling SDL events. */
  void step(SDL_Window *window);

  /** The script asked to quit. */
  bool wantsQuit();

  /**
   * While a script runs: queues its pointer position once more, after the ImGui backend has read
   * the OS pointer (which does not always follow the warp, e.g. while a screen locker holds the
   * pointer). Call between ImGui_ImplSDL3_NewFrame() and ImGui::NewFrame().
   */
  void holdPointer();
}
