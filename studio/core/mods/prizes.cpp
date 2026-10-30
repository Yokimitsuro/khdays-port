#include "core/mods/prizes.h"

#include "core/files/paths.h"
#include "core/mods/process.h"
#include "core/rooms/kh2_source.h"

#include "gfx/kh2_sequence.h"
#include "khdays/assets/image.h"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <sstream>

namespace fs = std::filesystem;

namespace studio {

namespace {

namespace prizes = khdays::ui::prizes;

std::vector<std::string> lines_of(const fs::path& file) {
    std::vector<std::string> out;
    std::ifstream in(file);
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        out.push_back(line);
    }
    return out;
}

// The first word of a line and the number after it (`size 1.5`), with no comment.
bool setting_of(const std::string& line, std::string* what, float* value) {
    std::istringstream words(line.substr(0, line.find('#')));
    return static_cast<bool>(words >> *what >> *value);
}

bool is_model(const fs::path& file) {
    const auto e = file.extension().string();
    return e == ".gltf" || e == ".glb" || e == ".fbx";
}

std::vector<fs::path> files_in(const fs::path& folder) {
    std::vector<fs::path> out;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (entry.is_regular_file(ec))
            out.push_back(entry.path());
    }
    std::sort(out.begin(), out.end());
    return out;
}

}  // namespace

std::vector<PrizePiece> prize_pieces(const fs::path& mod_dir) {
    std::vector<PrizePiece> out;
    const fs::path prize = mod_dir / "graphics" / "mo" / "prize.pak.z";
    for (const auto& file : files_in(prize / "fx")) {
        if (file.extension() == ".txt")
            out.push_back({PrizePiece::Kind::effect, u8(file.stem()), file, {}, "mo/prize.pak.z"});
    }
    for (const auto& file : files_in(prize / "mdl")) {
        if (!is_model(file))
            continue;
        PrizePiece p{PrizePiece::Kind::model, u8(file.stem()), file, fs::path(file).replace_extension(".txt"),
                     "mo/prize.pak.z"};
        for (const auto& line : lines_of(p.settings)) {
            std::string what;
            float value = 0;
            if (setting_of(line, &what, &value) && what == "size")
                p.size = value;
            else if (setting_of(line, &what, &value) && what == "turn")
                p.turn = value;
            else
                p.other.push_back(line);
        }
        out.push_back(std::move(p));
    }
    std::error_code ec;
    std::vector<fs::path> entries;
    for (const auto& entry : fs::directory_iterator(mod_dir / "graphics" / "mo", ec)) {
        if (entry.is_directory(ec) && u8(entry.path().filename()).rfind("mo#", 0) == 0)
            entries.push_back(entry.path());
    }
    std::sort(entries.begin(), entries.end());
    for (const auto& entry : entries) {
        for (const auto& file : files_in(entry / "mdl")) {
            const std::string name = u8(file.stem());
            if (!is_model(file) || name.size() < 6 || name.compare(name.size() - 6, 6, "_box_b") != 0)
                continue;
            PrizePiece p{PrizePiece::Kind::chest, name, file, fs::path(file).replace_extension(".bones.txt"),
                         "mo/" + u8(entry.filename())};
            for (const auto& line : lines_of(p.settings)) {
                std::string what;
                float value = 0;
                if (setting_of(line, &what, &value) && what == "scale")
                    p.scale = value;
                else
                    p.other.push_back(line);
            }
            out.push_back(std::move(p));
        }
    }
    // the chests by their names (tt_box_b ...), not their entries' numbers
    std::stable_sort(out.begin(), out.end(), [](const PrizePiece& a, const PrizePiece& b) {
        return a.kind == PrizePiece::Kind::chest && b.kind == PrizePiece::Kind::chest ? a.name < b.name
                                                                                      : a.kind < b.kind;
    });
    return out;
}

bool save_prize_piece(const PrizePiece& piece, std::string* why) {
    if (piece.kind == PrizePiece::Kind::effect) {
        *why = "an effect has nothing here to save";
        return false;
    }
    std::ostringstream text;
    for (const auto& line : piece.other)
        text << line << '\n';
    char number[64];
    if (piece.kind == PrizePiece::Kind::model) {
        std::snprintf(number, sizeof(number), "%g", static_cast<double>(piece.size));
        text << "size " << number << '\n';
        std::snprintf(number, sizeof(number), "%g", static_cast<double>(piece.turn));
        text << "turn " << number << '\n';
    } else {
        std::snprintf(number, sizeof(number), "%.6g", static_cast<double>(piece.scale));
        text << "scale " << number << '\n';
    }
    std::ofstream out(piece.settings, std::ios::binary);
    out << text.str();
    if (!out) {
        *why = "cannot write " + u8(piece.settings);
        return false;
    }
    return true;
}

std::optional<PrizeEffect> load_prize_effect(const fs::path& file, std::string* why) {
    std::ifstream in(file, std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto effect = prizes::parse_effect(text, *why);
    if (!effect)
        return std::nullopt;
    PrizeEffect e;
    e.effect = std::move(*effect);
    std::vector<khdays::assets::DecodedTexture> pages;
    std::vector<std::pair<int, int>> sizes;
    try {
        for (const auto& page : e.effect.pages) {
            pages.push_back(khdays::assets::read_image(file.parent_path() / path_u8(page)));
            sizes.emplace_back(pages.back().width, pages.back().height);
        }
    } catch (const std::exception& ex) {
        *why = ex.what();
        return std::nullopt;
    }
    e.layout = prizes::stack_pages(sizes);
    e.atlas.width = e.layout.width;
    e.atlas.height = e.layout.height;
    e.atlas.rgba.assign(static_cast<std::size_t>(e.layout.width) * static_cast<std::size_t>(e.layout.height) * 4, 0);
    for (std::size_t i = 0; i < pages.size(); ++i) {
        for (int y = 0; y < pages[i].height; ++y) {
            std::memcpy(
                &e.atlas
                     .rgba[(static_cast<std::size_t>(e.layout.top[i] + y) * static_cast<std::size_t>(e.layout.width)) *
                           4],
                &pages[i].rgba[static_cast<std::size_t>(y) * static_cast<std::size_t>(pages[i].width) * 4],
                static_cast<std::size_t>(pages[i].width) * 4);
        }
    }
    e.reach = prizes::reach_of(e.effect);
    if (!(e.reach.half > 0)) {
        *why = "its layers have no size";
        return std::nullopt;
    }
    return e;
}

std::vector<std::uint8_t> prize_effect_frame(const PrizeEffect& e, int frame, int side, std::uint32_t background) {
    const auto quads =
        prizes::frame_quads(e.effect, prizes::frame_at(e.effect, static_cast<unsigned>(frame)), e.reach, e.layout);
    // the quads on a canvas 2 x half square, drawn at `side` pixels: their places scaled
    auto scaled = quads;
    const float k = static_cast<float>(side) / (2.0F * e.reach.half);
    for (auto& q : scaled) {
        for (auto& v : q.vertices) {
            v.x *= k;
            v.y *= k;
        }
    }
    std::vector<std::uint32_t> canvas(static_cast<std::size_t>(side) * static_cast<std::size_t>(side), background);
    khdays::render::kh2::draw_sequence(scaled, e.atlas, e.layout.width, e.layout.height, canvas, side, side);
    std::vector<std::uint8_t> rgba(canvas.size() * 4);
    for (std::size_t i = 0; i < canvas.size(); ++i) {
        for (int c = 0; c < 4; ++c)
            rgba[i * 4 + static_cast<std::size_t>(c)] = static_cast<std::uint8_t>((canvas[i] >> (8 * c)) & 255U);
    }
    return rgba;
}

std::map<std::string, std::vector<std::string>> kh2_prizes_inputs(const fs::path& mod_inputs) {
    std::map<std::string, std::vector<std::string>> out;
    for (const auto& line : lines_of(mod_inputs)) {
        if (line.empty() || line[0] == '#')
            continue;
        const auto space = line.find(' ');
        if (space == std::string::npos)
            continue;
        const std::string archive = line.substr(0, space), entry = line.substr(space + 1);
        if ((archive.rfind("kh2_", 0) == 0 && entry.rfind("obj/", 0) == 0) || archive == "bbs_second")
            out[archive].push_back(entry);
    }
    return out;
}

bool make_kh2_prizes(const Settings& settings, const fs::path& mod_dir, const fs::path& port_exe,
                     std::vector<std::string>* lines, std::string* why) {
    const fs::path root = settings.root;
    const fs::path kh2 = kh2_folder(root), bbs2 = root / "saves" / "collection" / "bbs_second";
    const auto inputs = kh2_prizes_inputs(root / "installer" / "mod_inputs.txt");
    if (inputs.empty()) {
        *why = "no installer/mod_inputs.txt in " + u8(root) + " to say what the tool reads";
        return false;
    }
    for (const auto& [archive, entries] : inputs) {
        if (!take_out_entries(settings, archive, entries, archive.rfind("kh2_", 0) == 0 ? kh2 : bbs2, why))
            return false;
    }
    std::error_code ec;
    const fs::path own = root / "setup" / "python" / "python.exe";
    const fs::path tool = root / "tools" / "mods" / "kh2_prizes.py";
    if (!fs::is_regular_file(tool, ec)) {
        *why = "no " + u8(tool);
        return false;
    }
    Job job({fs::is_regular_file(own, ec) ? u8(own) : std::string("python"), u8(tool), "--kh2", u8(kh2), "--exe",
             u8(port_exe), "--bbs2", u8(bbs2), "--mod", u8(fs::absolute(mod_dir, ec))},
            root);
    while (!job.done())
        SDL_Delay(20);
    *lines = job.lines();
    if (job.exit_code() != 0) {
        *why = job.exit_code() < 0 ? "Python could not be started (" + job.error() + ")"
               : lines->empty()    ? "the tool failed"
                                   : lines->back();
        return false;
    }
    return true;
}

}  // namespace studio
