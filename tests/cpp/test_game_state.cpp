#include <cstdint>
#include <iostream>

#include "khdays/game/game_state.h"

namespace {

int failures = 0;

void expect(const bool condition, const char* what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
}

}  // namespace

int main() {
    using khdays::game::FieldRef;
    using khdays::game::GameState;

    // --- Bit packing is decomp-exact (func_02025640 / 68 / 94) ------------
    // bit 0 is the MSB of word 0.
    {
        GameState s;
        s.set_flag(0U);
        expect(s.flag(0U), "bit 0 reads back set");
        expect(s.raw_word(0U) == 0x80000000U, "bit 0 is the MSB of word 0");
        expect(s.word_count() == 1U, "one word backs bit 0");

        s.set_flag(31U);
        expect(s.raw_word(0U) == 0x80000001U, "bit 31 is the LSB of word 0");
        expect(s.word_count() == 1U, "bit 31 still in word 0");

        s.set_flag(32U);
        expect(s.raw_word(1U) == 0x80000000U, "bit 32 is the MSB of word 1");
        expect(s.word_count() == 2U, "bit 32 grows to a second word");
    }

    // --- Reads past what was set return 0, without growing ----------------
    {
        GameState s;
        expect(!s.flag(9000U), "unset bit reads 0");
        expect(s.word_count() == 0U, "reading does not allocate");
        expect(s.get_field({100U, 8U}) == 0U, "unset field reads 0");
        expect(s.word_count() == 0U, "reading a field does not allocate");
    }

    // --- clear_flag -------------------------------------------------------
    {
        GameState s;
        s.set_flag(5U);
        s.clear_flag(5U);
        expect(!s.flag(5U), "cleared bit reads 0");
        s.clear_flag(9999U);  // clearing an unset far bit must not crash/grow
        expect(s.word_count() == 1U, "clearing past the end does not allocate");
    }

    // --- Multi-bit fields: offset bit is the MSB (== single-bit) ----------
    {
        GameState s;
        s.set_field({0U, 4U}, 0x8U);  // 0b1000 -> only the first (MSB) bit
        expect(s.flag(0U), "field MSB maps to the offset bit");
        expect(!s.flag(1U) && !s.flag(2U) && !s.flag(3U), "field low bits clear");
        expect(s.raw_word(0U) == 0x80000000U, "0x8 in a 4-bit field at 0 == MSB");
        expect(s.get_field({0U, 1U}) == (s.flag(0U) ? 1U : 0U),
               "get_field({n,1}) equals flag(n)");
    }

    // --- Round-trip, including across a word boundary ---------------------
    {
        GameState s;
        s.set_field({30U, 4U}, 0xBU);  // bits 30,31,32,33 -> spans two words
        expect(s.get_field({30U, 4U}) == 0xBU, "field round-trips across words");
        expect(s.word_count() == 2U, "cross-word field touches two words");

        // Exact bit placement, matching the DS BitArray_SetField
        // (func_02025754) MSB-first: 0xB = 1011 -> bit30=1, bit31=0 (word 0's
        // low 2 bits = 0b10 = 0x2) and bit32=1, bit33=1 (word 1's top 2 bits =
        // 0xC0000000). Traced by hand against the decompiled algorithm.
        expect(s.raw_word(0U) == 0x00000002U, "cross-word low half matches DS");
        expect(s.raw_word(1U) == 0xC0000000U, "cross-word high half matches DS");

        s.set_field({30U, 4U}, 0x0U);  // set-then-clear clears every bit
        expect(s.get_field({30U, 4U}) == 0U, "field can be cleared to 0");
    }

    // --- The day / story counter anchor (field 0,9) -----------------------
    {
        GameState s;
        s.set_day(0x165U);  // the story milestone ov008 checks
        expect(s.day() == 0x165U, "day counter round-trips (0x165)");
        expect(s.day() == s.get_field(GameState::kDayCounter),
               "day() is field (0,9)");

        s.set_day(0x47U);
        expect(s.day() == 0x47U, "day counter round-trips (0x47)");
    }

    // --- Progression flags ------------------------------------------------
    {
        GameState s;
        expect(!s.flag(GameState::kDetailSceneEntered), "flag starts clear");
        s.set_flag(GameState::kDetailSceneEntered);
        expect(s.flag(GameState::kDetailSceneEntered), "flag sets");
        expect(!s.flag(GameState::kMenuPresetsUnlocked),
               "distinct flags are independent");
    }

    // --- Preset fields: id -> (id*4 + 0x92b, 4), OR-merged ---------------
    {
        GameState s;
        const FieldRef f0 = GameState::preset_field(0U);
        expect(f0.offset == 0x92bU && f0.width == 4U, "preset_field(0) offset");
        const FieldRef f3 = GameState::preset_field(3U);
        expect(f3.offset == 0x92bU + 12U, "preset_field(3) offset");

        // ApplyFlagPresets OR-merges: v = preset | current; SetField(v).
        s.set_field(f0, 0x1U);
        s.set_field(f0, 0x1U | s.get_field(f0));  // OR in 0x1 again -> 0x1
        expect(s.get_field(f0) == 0x1U, "OR-merge keeps set bits");
        s.set_field(f0, 0x4U | s.get_field(f0));  // OR in 0x4 -> 0x5
        expect(s.get_field(f0) == 0x5U, "OR-merge accumulates bits");
    }

    if (failures == 0) {
        std::cout << "all game-state tests passed\n";
    }
    return failures == 0 ? 0 : 1;
}
