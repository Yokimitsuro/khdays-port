// The KH2 prizes mod's pieces in a mod (core/mods/prizes.h): read, a prize model's size and turn and a chest's scale
// written back with their other lines kept, an effect's frame drawn as the port draws it, and which of the
// collection's entries the tool reads. Made-up files only.
#include "test.h"

#include "core/mods/prizes.h"

extern "C" {
#include "render/png_io.h"
}

#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace studio;

namespace {

std::string read(const std::filesystem::path& file) {
    std::ifstream in(file, std::ios::binary);
    return std::string((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
}

std::filesystem::path made_up_mod() {
    const auto mod = studio_test::scratch("prizes") / "Prizes";
    const auto prize = mod / "graphics" / "mo" / "prize.pak.z";
    // an effect: one page, a white left half and a red right half, two layers on the left half
    studio_test::write(prize / "fx" / "prize_01.txt",
                       "effect 1\npage 0 page.png\nbody 0\nframe\n"
                       "layer page 0 rect 0 0 0.5 1 corners -10 -10 10 -10 -10 10 10 10 colour 1 1 1 1 blend alpha\n");
    std::vector<std::uint8_t> page(16 * 8 * 4, 255);
    for (int y = 0; y < 8; ++y)
        for (int x = 8; x < 16; ++x)
            page[static_cast<std::size_t>((y * 16 + x) * 4 + 1)] =
                page[static_cast<std::size_t>((y * 16 + x) * 4 + 2)] = 0;
    CHECK(khdays_png_write((prize / "fx" / "page.png").string().c_str(), page.data(), 16, 8) == nullptr);
    studio_test::write(prize / "mdl" / "prize_05.gltf", "{}");
    studio_test::write(prize / "mdl" / "prize_05.txt", "# a box\nsize 1.5\n");
    const auto chest = mod / "graphics" / "mo" / "mo#102" / "mdl";
    studio_test::write(chest / "tt_box_b.gltf", "{}");
    studio_test::write(chest / "tt_box_b.bones.txt", "j3 box1\nscale 0.0102\nkeep light01 light02\n");
    studio_test::write(chest / "tt_box.gltf", "{}");  // a crate, not a chest
    return mod;
}

}  // namespace

TEST(prizes_pieces_read_and_saved) {
    const auto mod = made_up_mod();
    auto pieces = prize_pieces(mod);
    CHECK(pieces.size() == 3);
    CHECK(pieces[0].kind == PrizePiece::Kind::effect && pieces[0].name == "prize_01");
    CHECK(pieces[1].kind == PrizePiece::Kind::model && pieces[1].name == "prize_05");
    CHECK(pieces[1].size == 1.5f && pieces[1].turn == 90.0f);  // its turn the port's default
    CHECK(pieces[2].kind == PrizePiece::Kind::chest && pieces[2].name == "tt_box_b" && pieces[2].where == "mo/mo#102");
    CHECK(pieces[2].scale > 0.0101f && pieces[2].scale < 0.0103f);

    std::string why;
    pieces[1].size = 2.0f;
    pieces[1].turn = 45.0f;
    CHECK(save_prize_piece(pieces[1], &why));
    pieces[2].scale = 0.02f;
    CHECK(save_prize_piece(pieces[2], &why));
    CHECK(!save_prize_piece(pieces[0], &why));  // an effect: nothing of it saved here
    const auto again = prize_pieces(mod);
    CHECK(again[1].size == 2.0f && again[1].turn == 45.0f);
    CHECK(again[2].scale == 0.02f);
    const std::string bones = read(again[2].settings);
    CHECK(bones.find("j3 box1\n") != std::string::npos && bones.find("keep light01 light02\n") != std::string::npos);
    CHECK(read(again[1].settings).find("# a box\n") != std::string::npos);  // its other lines kept
}

TEST(prizes_effect_frame) {
    const auto mod = made_up_mod();
    std::string why;
    const auto e = load_prize_effect(mod / "graphics" / "mo" / "prize.pak.z" / "fx" / "prize_01.txt", &why);
    CHECK(e.has_value());
    const int side = 20;
    const auto rgba = prize_effect_frame(*e, 0, side, 0xff000000u);
    CHECK(rgba.size() == static_cast<std::size_t>(side * side * 4));
    // the layer covers the canvas with the page's white left half
    const std::size_t middle = static_cast<std::size_t>((10 * side + 10) * 4);
    CHECK(rgba[middle] > 240 && rgba[middle + 1] > 240 && rgba[middle + 2] > 240);
    CHECK(!load_prize_effect(mod / "nothing.txt", &why).has_value() && !why.empty());
}

TEST(prizes_collection_inputs) {
    const auto dir = studio_test::scratch("prize-inputs");
    studio_test::write(
        dir / "mod_inputs.txt",
        "# made up\n# tools 0\nkh2_first obj/PRIZE.mdlx\nkh2_first menu/us/save.2ld\n"
        "kh2_fifth obj/F_EX030.mdlx\nbbs_second ForEventJP/gimmick/g01pp/00/g01pp00.pmo\nRecom FORM/x\n");
    const auto inputs = kh2_prizes_inputs(dir / "mod_inputs.txt");
    CHECK(inputs.size() == 4);
    CHECK(inputs.at("kh2_first") == std::vector<std::string>{"obj/PRIZE.mdlx"});
    CHECK(inputs.at("kh2_fifth") == std::vector<std::string>{"obj/F_EX030.mdlx"});
    CHECK(inputs.at("bbs_second").size() == 1);
    CHECK(inputs.at("Recom") == std::vector<std::string>{"FORM/x"});
}
