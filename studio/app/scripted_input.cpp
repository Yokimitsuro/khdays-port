// A person's mouse and keys from a file (scripted_input.h).
#include "scripted_input.h"

#include "imgui.h"

#include <cstdio>
#include <fstream>
#include <iterator>
#include <sstream>

namespace studio {

namespace {

ImGuiKey key_named(const std::string& name) {
    for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
        if (name == ImGui::GetKeyName(static_cast<ImGuiKey>(k)))
            return static_cast<ImGuiKey>(k);
    }
    return ImGuiKey_None;
}

}  // namespace

ScriptedInput::ScriptedInput(std::string file) : file_(std::move(file)) {
    std::fprintf(stderr, "khdays-studio: the mouse and keys read from %s\n", file_.c_str());
}

bool ScriptedInput::step() {
    ImGuiIO& io = ImGui::GetIO();
    // the place given again: the backend puts the person's own mouse there while the window has the focus
    io.AddMousePosEvent(x_, y_);
    // whole lines appended since
    {
        std::ifstream in(file_, std::ios::binary);
        if (in) {
            in.seekg(static_cast<std::streamoff>(taken_));
            const std::string more((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            const std::size_t end = more.rfind('\n');
            if (end != std::string::npos) {
                std::istringstream lines(more.substr(0, end + 1));
                for (std::string line; std::getline(lines, line);) {
                    if (!line.empty() && line.back() == '\r')
                        line.pop_back();
                    if (!line.empty())
                        steps_.push_back(line);
                }
                taken_ += end + 1;
            }
        }
    }
    if (waiting_ > 0) {
        --waiting_;
        return true;
    }
    if (steps_.empty())
        return true;
    const std::string line = steps_.front();
    steps_.pop_front();
    std::istringstream words(line);
    std::string what;
    words >> what;
    if (what == "move" || what == "click") {
        words >> x_ >> y_;
        io.AddMousePosEvent(x_, y_);
        if (what == "click") {  // pressed on the next frame, the place hovered first; let go on the one after
            steps_.push_front("release");
            steps_.push_front("press");
        }
    } else if (what == "press" || what == "release") {
        io.AddMouseButtonEvent(ImGuiMouseButton_Left, what == "press");
    } else if (what == "wheel") {
        float n = 0.0f;
        words >> n;
        io.AddMouseWheelEvent(0.0f, n);
    } else if (what == "key" || what == "key-up") {
        std::string name;
        words >> name;
        const ImGuiKey key = key_named(name);
        if (key == ImGuiKey_None) {
            std::fprintf(stderr, "khdays-studio: no key named %s\n", name.c_str());
        } else {
            io.AddKeyEvent(key, what == "key");
            if (what == "key")
                steps_.push_front("key-up " + name);
        }
    } else if (what == "text") {
        std::string rest;
        std::getline(words, rest);
        if (!rest.empty() && rest.front() == ' ')
            rest.erase(0, 1);
        io.AddInputCharactersUTF8(rest.c_str());
    } else if (what == "wait") {
        words >> waiting_;
    } else if (what == "quit") {
        return false;
    } else {
        std::fprintf(stderr, "khdays-studio: an input line not understood: %s\n", line.c_str());
    }
    return true;
}

}  // namespace studio
