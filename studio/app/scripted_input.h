// A person's mouse and keys from a file, for driving the studio's window from a script (checks of what only the
// window shows: a script appends lines to the file, captures the window, and appends more). The file is named by the
// environment variable KHDAYS_STUDIO_INPUT; one line a frame, each a step:
//   move X Y      the mouse to X, Y (the window's pixels, as a capture of it counts them)
//   click X Y     the left button pressed and let go there
//   wheel N       the wheel turned N steps (up when positive)
//   key NAME      a key pressed and let go (Dear ImGui's name: Enter, Escape, Tab, DownArrow, A...)
//   text WORDS    the words typed into the field being edited
//   wait FRAMES   nothing for that many frames
//   quit          the studio closed, as by its window's close button
// While it is set, the window opens without taking the focus from what the person is using.
#pragma once

#include <cfloat>
#include <deque>
#include <string>

namespace studio {

class ScriptedInput {
public:
    explicit ScriptedInput(std::string file);
    // The lines appended since the last frame read and this frame's step given to Dear ImGui (after the backend's
    // own events, before ImGui::NewFrame); false once quit is asked.
    bool step();

private:
    std::string file_;
    std::size_t taken_ = 0;          // the bytes of the file read so far
    std::deque<std::string> steps_;  // lines and their parts (a click's press and release) left to give
    int waiting_ = 0;
    float x_ = -FLT_MAX, y_ = -FLT_MAX;  // where the mouse was put last, given again every frame (none yet)
};

}  // namespace studio
