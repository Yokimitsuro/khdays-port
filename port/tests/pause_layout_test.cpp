// Synthetic native pause tiles exercise proportions and Retry selection through the actual emitter.
#include "../ui/pause.cpp"
#include <iostream>
namespace {
std::vector<std::vector<unsigned>> images;
std::vector<KhdaysUiElement> emitted;
std::array<unsigned char, 32 * 32 * 64> tiles{};
KhdaysRetryView current_retry{};
void require(bool ok, const char *why)
{
    if (!ok)
        throw std::runtime_error(why);
}
void line(int row, unsigned char color)
{
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 88; ++x) {
            int cx = 10 + x / 8, cy = row + y / 8;
            tiles[(cy * 32 + cx) * 64 + (y % 8) * 8 + x % 8] = (x == 30 && y == 4) ? 7 : color;
        }
}
} // namespace
extern "C" {
unsigned char *khdays_window;
int khdays_bg_text_layer(int, int, KhdaysBgLayer *l)
{
    *l = {};
    l->wt = l->ht = 32;
    l->colors256 = 1;
    return 1;
}
uint32_t khdays_bg_map_entry(int, const KhdaysBgLayer *, int x, int y)
{
    return unsigned(y * 32 + x);
}
const uint8_t *khdays_bg_memory(int, uint32_t at)
{
    return at < tiles.size() ? tiles.data() + at : nullptr;
}
int khdays_ui_scale(void)
{
    return 1;
}
int khdays_ui_image_get(int w, int h)
{
    images.emplace_back(size_t(w * h));
    return int(images.size() - 1);
}
uint32_t *khdays_ui_image_begin(int i)
{
    return images.at(i).data();
}
void khdays_ui_image_end(int) {}
void khdays_ui_image_put(int) {}
void khdays_ui_emit(int, const KhdaysUiElement *e)
{
    emitted.push_back(*e);
}
void khdays_ui_left_out_cell(int, int, int, int, int) {}
void khdays_ui_register(int, void (*)(void)) {}
int khdays_pause_view(KhdaysPauseView *)
{
    return 0;
}
int khdays_retry_view(KhdaysRetryView *v)
{
    *v = current_retry;
    return v->shown;
}
int khdays_mods_find(const char *, char *, int)
{
    return 0;
}
uint8_t *khdays_png_read(const char *, int *, int *, const char **)
{
    return nullptr;
}
void khdays_png_free(uint8_t *) {}
unsigned khdays_ui_hud_options(void)
{
    return 0;
}
int khdays_link_mission_target(int *, int *)
{
    return 0;
}
KhdaysRomReader *khdays_rom_reader_new(void)
{
    return nullptr;
}
int khdays_rom_file_data(KhdaysRomReader *, const char *, int, u8 **, u32 *)
{
    return 0;
}
void khdays_log(const char *, ...) {}
const char *khdays_text_in(int, int)
{
    return "Retry";
}
}
namespace khdays::vfs {
std::optional<std::filesystem::path> resolve(std::string_view)
{
    return std::nullopt;
}
} // namespace khdays::vfs
namespace khdays::assets {
Font decode_nftr(const std::filesystem::path &)
{
    throw std::runtime_error("unused font read");
}
DecodedTexture render_text(const Font &, const std::u16string &)
{
    DecodedTexture out;
    out.width = out.height = 1;
    out.rgba = {255, 255, 255, 255};
    return out;
}
} // namespace khdays::assets
int main()
{
    try {
        khdays_window = static_cast<unsigned char *>(std::calloc(0x05001000, 1));
        require(khdays_window != nullptr, "test memory");
        auto *pal = static_cast<unsigned short *>(KHDAYS_W(0x05000000));
        pal[1] = 31;
        pal[2] = 0x0421;
        pal[7] = 0x7fff;
        line(11, 1);
        line(13, 2);
        KhdaysPauseView pv{};
        pv.count = 4;
        pv.layer = 2;
        pv.language = 1;
        pv.piece[0] = {KHDAYS_PAUSE_TITLE, 10, 8, 12, 2, 0};
        pv.piece[1] = {KHDAYS_PAUSE_CONTINUE, 10, 11, 11, 2, 1};
        pv.piece[2] = {KHDAYS_PAUSE_RETRY, 10, 13, 11, 2, 0};
        pv.piece[3] = {KHDAYS_PAUSE_WITHDRAW, 10, 15, 11, 2, 0};
        font.emplace();
        font_tried = true;
        target_layout = true;
        current_retry = {1, 0, 0, 1};
        require(emit_native_target_menu(pv), "initial native pause");
        require(emitted[0].h == 16 && emitted[0].w == 96, "native title was stretched");
        require(emitted[0].y + emitted[0].h == 48, "pause title must end six units above the target");
        for (int cursor = 1; cursor <= 2; ++cursor) {
            emitted.clear();
            line(11, 2);
            line(13, 1);
            current_retry = {1, cursor, 2, 1};
            require(emit_native_target_menu(pv), "retry native pause");
            for (int i = 0; i < 3; ++i) {
                auto pixel = images.at(emitted.at(i + 1).image).at(0);
                require(pixel == palette_colour(0, i == cursor ? 1 : 2), "multiple native buttons selected");
            }
        }
        std::free(khdays_window);
        std::cout << "native pause layout passed\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << e.what() << '\n';
        return 1;
    }
}
