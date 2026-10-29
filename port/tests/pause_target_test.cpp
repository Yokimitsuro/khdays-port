// Synthetic target tiles verify palette selection, mirroring and safe bounds.
#include "khdays/assets/graphics2d.h"
#include <iostream>
#include <stdexcept>

int main()
{
    using namespace khdays::assets;
    try {
        Tilemap map;
        map.width_tiles = map.height_tiles = 1;
        map.cells.resize(1);
        map.cells[0].palette = 1;
        TileGraphics tiles;
        tiles.tile_count = 1;
        tiles.indices.resize(64, 1);
        tiles.indices[0] = 0;
        Palette2D palette;
        palette.colors.resize(32, {0, 0, 0, 255});
        palette.colors[17] = {255, 40, 20, 255};
        auto require = [](bool ok) {
            if (!ok)
                throw std::runtime_error("target tile reconstruction failed");
        };
        const auto image = compose_background(map, tiles, palette, false);
        require(image.width == 8 && image.height == 8 && image.rgba[3] == 255 && image.rgba[4] == 255 &&
                image.rgba[5] == 40);
        map.cells[0].flip_h = map.cells[0].flip_v = true;
        const auto flipped = compose_background(map, tiles, palette);
        require(flipped.rgba[3] == 255 && flipped.rgba.back() == 0);
        tiles.bpp = 8;
        palette.colors[1] = {10, 20, 30, 255};
        require(compose_background(map, tiles, palette).rgba[0] == 10);
        map.cells[0].tile = 1;
        bool rejected = false;
        try {
            compose_background(map, tiles, palette);
        } catch (const std::runtime_error &) {
            rejected = true;
        }
        require(rejected);
        std::cout << "pause target tiles passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
