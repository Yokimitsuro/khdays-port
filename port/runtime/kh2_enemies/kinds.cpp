// The KH2 enemy kinds of the player's mod (kinds.h).
#include "kinds.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace khdays::kh2_enemies {
namespace {

std::string upper(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return s;
}

std::vector<std::string> split_tabs(const std::string &line)
{
    std::vector<std::string> out;
    std::string cell;
    std::istringstream in(line);
    while (std::getline(in, cell, '\t'))
        out.push_back(cell);
    return out;
}

} // namespace

std::vector<Kind> read_kinds(const std::string &text)
{
    std::vector<Kind> out;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line[0] == '#')
            continue;
        const auto cells = split_tabs(line);
        if (cells.size() < 4 || cells[0].empty())
            continue;
        Kind k;
        k.name = upper(cells[0]);
        k.title = cells[1].empty() ? k.name : cells[1];
        k.covered = cells[2] == "yes";
        k.calls = std::atoi(cells[3].c_str());
        k.missing = cells.size() > 4 ? cells[4] : "-";
        k.plays_player = cells.size() > 5 && cells[5] == "player"; // (a list made before the column: none)
        out.push_back(std::move(k));
    }
    return out;
}

const Kind *find_kind(const std::vector<Kind> &kinds, const std::string &name)
{
    const std::string key = upper(name);
    for (const auto &k : kinds)
        if (k.name == key)
            return &k;
    return nullptr;
}

std::vector<Kind> load_kinds(const std::string &folder, const std::function<void(const std::string &)> &why)
{
    namespace fs = std::filesystem;
    const fs::path base(std::u8string(folder.begin(), folder.end()));
    std::ifstream in(base / "kh2_enemies.txt", std::ios::binary);
    if (!in) {
        why("kh2 enemies: no list in " + folder);
        return {};
    }
    std::stringstream text;
    text << in.rdbuf();
    std::vector<Kind> kept;
    std::error_code ec;
    for (auto &k : read_kinds(text.str())) {
        const fs::path obj = base / "kh2" / "obj";
        // its .mdlx (its script, its shapes); its .mset when it has motions of its own (the Possessor has none)
        if (!fs::is_regular_file(obj / (k.name + ".mdlx"), ec)) {
            why("kh2 enemies: " + k.name + "'s .mdlx is missing; left out");
            continue;
        }
        kept.push_back(std::move(k));
    }
    return kept;
}

} // namespace khdays::kh2_enemies
