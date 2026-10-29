// High-resolution atlas metadata for an original NitroFS font resource.
#ifndef KHDAYS_HD_FONTS_H
#define KHDAYS_HD_FONTS_H

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace khdays::hd_fonts {

struct Glyph {
    bool original = false;                // Explicit native artwork and metrics; the index remains reserved.
    bool secondary = false;               // Skip primary artwork without shifting later glyphs.
    int x = 0, y = 0, w = 0, h = 0;       // reference-atlas pixels, NOT output size
    int left = 0, width = 0, advance = 0; // Days units, or source units with explicit scale
    // Actual image-pixel rectangle, scaled from the reference atlas.
    double image_x = 0, image_y = 0, image_w = 0, image_h = 0;
    mutable bool ink_ready = false;
    mutable int ink_left = 0, ink_right = 0;
};
struct Mapping {
    std::uint32_t codepoint = 0;
    std::uint32_t glyph = 0;
};
struct Atlas {
    std::string source;
    std::string png_path;
    int width = 0, height = 0; // actual replacement PNG dimensions
    int reference_width = 0, reference_height = 0;
    int cell_width = 0, cell_height = 0, baseline = 0, line_feed = 0;
    int baseline_offset = 0; // Final logical pixels; positive moves artwork down.
    int max_width = 0;
    bool sized = false; // Explicit scale opts into replacement cell/metric units.
    double scale = 1;
    std::vector<Glyph> glyphs;
    std::vector<Mapping> charmap;
    std::unordered_set<std::uint32_t> original_codepoints; // Source-specific native symbols, including routed views.
    bool has_original = false;
    bool has_secondary = false, native_metrics = false, primary_missing = false;
    std::shared_ptr<const Atlas> secondary;
    mutable bool codes_bound = false;
    mutable std::unordered_set<std::uint32_t> original_codes; // No replacement kerning across native glyphs.
    mutable std::vector<int> original_glyphs;  // Original NFTR index -> replacement index, -1 when absent.
    mutable std::vector<int> secondary_glyphs; // Original NFTR index -> secondary index, -1 when absent.
    mutable std::unordered_set<std::uint32_t> secondary_codes; // Kerning stays within the resolved font.
    std::unordered_map<std::uint64_t, int> kerning;            // Unicode pair: left << 32 | right
    std::vector<std::uint8_t> rgba;                            // actual_width * actual_height * 4
    std::shared_ptr<const Atlas> artwork; // Routed views share pixels but bind their own original CMAP.
    const std::vector<std::uint8_t> &pixels() const
    {
        return artwork ? artwork->pixels() : rgba;
    }
};

// Finds mods/<Mod>/fonts/<original-resource-path>.png, reading a .json beside it
// from the SAME mod. Returns null for no mod or invalid data; errors are logged.
// An original NFTR can be 'text/font_eu_10.nftr' or 'sf/ssfont_8.nftr'.
std::shared_ptr<const Atlas> load(const std::string &original_resource);
// Capture keeps configured artwork identities while the display preference is off.
bool observe(bool enabled);
int layout_override(int mode);
std::shared_ptr<const Atlas> load_configured(const std::string &original_resource);

// Called by the offline graphics dump for an initial independent smoke test.
// Return: 0=no override, 1=valid, -1=invalid override.
int probe(const std::string &original_resource);

// Stable session identity for a scoped replacement; an empty result means no mapping.
std::string select(const std::string &original_resource, const std::string &scope);
bool mappings_enabled();
// Opt-in numeric layouts, resolved once per session; unknown IDs retain game positioning.
bool numeric_layout(int layout);
double fit_layout(int layout, double *offset);

} // namespace khdays::hd_fonts
#endif
