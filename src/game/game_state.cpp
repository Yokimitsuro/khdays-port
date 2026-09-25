#include "khdays/game/game_state.h"

namespace khdays::game {

namespace {

// Decomp-exact bit addressing (func_02025640 / 68 / 94): bit `n` is the
// (31 - n % 32)-th bit of word (n / 32) -- bit 0 is the MSB of word 0.
constexpr std::size_t word_of(const std::uint32_t bit) { return bit / 32U; }
constexpr std::uint32_t mask_of(const std::uint32_t bit) {
    return 1U << (31U - (bit & 31U));
}

}  // namespace

void GameState::ensure_word(const std::size_t index) {
    if (index >= words_.size()) {
        words_.resize(index + 1U, 0U);
    }
}

bool GameState::flag(const std::uint32_t bit) const {
    const std::size_t word = word_of(bit);
    if (word >= words_.size()) {
        return false;  // never set -> 0
    }
    return (words_[word] & mask_of(bit)) != 0U;
}

void GameState::set_flag(const std::uint32_t bit) {
    const std::size_t word = word_of(bit);
    ensure_word(word);
    words_[word] |= mask_of(bit);
}

void GameState::reset_to_new_game(int difficulty) {
    words_.clear();
    set_field({0x000U, 9U}, 0x191U);
    difficulty = difficulty < 0 ? 0 : (difficulty > 3 ? 3 : difficulty);
    set_field({0x40aU, 2U}, static_cast<std::uint32_t>(difficulty));
    // func_ov000_02054c50's default list, in its order: {bit, width, value}.
    constexpr std::uint32_t kDefaults[][3] = {
        {0x37c4U, 1U, 0U}, {0x37bfU, 1U, 0U}, {0x37c0U, 2U, 1U},
        {0x37c3U, 1U, 0U}, {0x37c2U, 1U, 0U}, {0x37c5U, 1U, 0U},
        {0x37c6U, 1U, 1U}, {0x37c7U, 2U, 0U}, {0x35bfU, 2U, 0U},
        {0x3c15U, 1U, 1U}, {0x3c16U, 1U, 1U}, {0x3c17U, 2U, 0U},
        {0x3c19U, 2U, 0U}, {0x3c1bU, 2U, 2U}, {0x3c1dU, 2U, 1U},
        {0x3c26U, 1U, 1U}, {0x3c1fU, 1U, 0U}, {0x3c20U, 1U, 1U},
        {0x35c1U, 2U, 1U}, {0x3c23U, 2U, 1U}, {0x3c21U, 2U, 1U},
        {0x3c25U, 1U, 0U}, {0x3c27U, 2U, 1U}, {0x3c29U, 2U, 1U},
        {0x0ab3U, 4U, 8U}, {0x095bU, 4U, 8U},
    };
    for (const auto& d : kDefaults) {
        set_field({d[0], d[1]}, d[2]);
    }
    // INITi_CpuClear32(-1, work + 0x198c, 0x320); the bit array itself starts
    // at work + 0x10, so these are its words from byte 0x197c.
    constexpr std::size_t kFirst = 0x197cU / 4U;
    constexpr std::size_t kCount = 0x320U / 4U;
    ensure_word(kFirst + kCount - 1U);
    for (std::size_t i = 0; i < kCount; ++i) {
        words_[kFirst + i] = 0xFFFFFFFFU;
    }
}

void GameState::clear_flag(const std::uint32_t bit) {
    const std::size_t word = word_of(bit);
    if (word < words_.size()) {
        words_[word] &= ~mask_of(bit);
    }
}

std::uint32_t GameState::get_field(const FieldRef ref) const {
    // The bit at ref.offset is the value's MSB, matching the single-bit
    // convention (get_field({n, 1}) == flag(n)) and the DS's own MSB-first
    // BitArray_GetField (khdays-decomp func_020256b8).
    std::uint32_t value = 0U;
    for (std::uint32_t i = 0U; i < ref.width; ++i) {
        value = (value << 1U) | (flag(ref.offset + i) ? 1U : 0U);
    }
    return value;
}

void GameState::set_field(const FieldRef ref, const std::uint32_t value) {
    for (std::uint32_t i = 0U; i < ref.width; ++i) {
        const std::uint32_t bit_value =
            (value >> (ref.width - 1U - i)) & 1U;
        if (bit_value != 0U) {
            set_flag(ref.offset + i);
        } else {
            clear_flag(ref.offset + i);
        }
    }
}

}  // namespace khdays::game
