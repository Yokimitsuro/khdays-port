#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace khdays::assets {

// The named contents of an SDAT sound archive, by category. Names come from the
// SYMB block; a category may be empty if the archive omits its symbols.
struct SdatInventory final {
    std::vector<std::string> sequences;          // SEQ  — music and sfx sequences
    std::vector<std::string> sequence_archives;  // SEQARC
    std::vector<std::string> banks;              // BANK — instrument banks
    std::vector<std::string> wave_archives;      // WAVEARC
    std::vector<std::string> players;            // PLAYER
    std::vector<std::string> groups;             // GROUP
    std::vector<std::string> stream_players;     // PLAYER2
    std::vector<std::string> streams;            // STRM — streamed audio
};

// Read the metadata inventory of an SDAT file. Throws std::runtime_error on
// malformed data.
SdatInventory read_sdat_inventory(const std::filesystem::path& input_path);

}  // namespace khdays::assets
