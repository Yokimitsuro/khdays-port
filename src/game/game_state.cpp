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

void GameState::clear_flag(const std::uint32_t bit) {
    const std::size_t word = word_of(bit);
    if (word < words_.size()) {
        words_[word] &= ~mask_of(bit);
    }
}

std::uint32_t GameState::get_field(const FieldRef ref) const {
    // The bit at ref.offset is the value's MSB, matching the single-bit
    // convention (get_field({n, 1}) == flag(n)).
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
