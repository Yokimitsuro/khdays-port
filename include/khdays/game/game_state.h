#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

// The game's persistent progression store.
//
// On the DS this is a single packed bit array (khdays-decomp: the store at
// `data_0204be18 + 0x10`, reached through `GameState_GetField`/`SetField` =
// `func_020235d0`/`func_020235e8`, and the single-bit flag test
// `func_02023588`). Everything the front-end and the in-game menu gate on --
// the day/story counter, the menu progression flags, the 4-bit preset fields --
// lives in this one bitset addressed by (bit offset, bit width). See
// docs/OV008_MENU.md.
//
// This is the port's neutral model of that store. It is a foundation: no scene
// reads it yet, and the port has no DS-save reader, so it holds only what the
// running port sets. Because it never has to round-trip a DS save file, it does
// not need the DS's exact byte layout -- only self-consistent addressing.
namespace khdays::game {

// A (bit offset, bit width) window into the store, matching the DS's
// (field, width) argument pair.
struct FieldRef {
    std::uint32_t offset;
    std::uint32_t width;
};

class GameState {
  public:
    GameState() = default;

    // --- Single bits (flags) ---------------------------------------------
    // Bit addressing is decomp-exact: bit `n` is word `n / 32`, position
    // `31 - (n & 31)` -- i.e. bit 0 is the MSB of word 0 (khdays-decomp
    // func_02025640 set / func_02025668 clear / func_02025694 test).
    [[nodiscard]] bool flag(std::uint32_t bit) const;
    void set_flag(std::uint32_t bit);
    void clear_flag(std::uint32_t bit);

    // --- Multi-bit fields -------------------------------------------------
    // Reads/writes `ref.width` consecutive bits starting at `ref.offset`, in
    // the same big-endian numbering as the single-bit primitives: the bit at
    // `ref.offset` is the most significant of the value, so get_field({n, 1})
    // equals flag(n). This is byte-behaviour-identical to the DS's own
    // multi-bit accessors BitArray_GetField / BitArray_SetField
    // (khdays-decomp func_020256b8 / func_02025754), which are MSB-first and
    // span word boundaries the same way -- verified in the test.
    [[nodiscard]] std::uint32_t get_field(FieldRef ref) const;
    void set_field(FieldRef ref, std::uint32_t value);

    // --- Named anchors (from khdays-decomp) ------------------------------
    // The day / story counter: field (offset 0, width 9). ov008 gates on it
    // reaching 0x47 (Ov008_Menu_ApplyFlagPresets) and 0x165 (layout variant in
    // Ov008_MainMenu_StateTick / SetupTextSurfaces); 9 bits hold 0..511, which
    // spans the game's day range.
    static constexpr FieldRef kDayCounter{0U, 9U};

    [[nodiscard]] std::uint32_t day() const { return get_field(kDayCounter); }
    void set_day(std::uint32_t day) { set_field(kDayCounter, day); }

    // The 4-bit preset fields Ov008_Menu_ApplyFlagPresets OR-merges are
    // addressed by `id * 4 + 0x92b`. Exposed as a helper so callers name the id,
    // not the raw offset.
    [[nodiscard]] static constexpr FieldRef preset_field(std::uint32_t id) {
        return FieldRef{id * 4U + 0x92bU, 4U};
    }

    // Progression flags observed in the ov008 menu code. Named by the role the
    // decomp comments give them; they are plain bit indices in the store.
    enum Flag : std::uint32_t {
        // Gates Ov008_Menu_ApplyFlagPresets' second preset table.
        kMenuPresetsUnlocked = 0x200bU,
        // Set by Ov008_Menu_CommitEnterSubScene8 ("sets the persistent game
        // flag 0x200c"); once set, the menu treats the detail sub-scene as
        // entered and the sub-item grid as fully unlocked.
        kDetailSceneEntered = 0x200cU,
        // Gates a main-menu toolbar entry in Ov008_MainMenu_SetupToolbar.
        kToolbarStoryGate = 0x200dU,
    };

    // Number of 32-bit words currently backing the store (grows as bits are
    // set; reads past the end return 0). Exposed for tests.
    [[nodiscard]] std::size_t word_count() const { return words_.size(); }

    // Raw backing word (0 past the end). Test-only: lets a test check the bit
    // packing against the DS's word layout, not just round-trip behaviour.
    [[nodiscard]] std::uint32_t raw_word(std::size_t index) const {
        return index < words_.size() ? words_[index] : 0U;
    }

  private:
    void ensure_word(std::size_t index);

    std::vector<std::uint32_t> words_;
};

}  // namespace khdays::game
