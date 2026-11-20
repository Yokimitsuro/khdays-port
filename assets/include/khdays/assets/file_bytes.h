// A whole file read into memory, its errors naming what kind of file it is ("model", "animation", "SDAT"): one copy
// for the decoders that take a path.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <ios>
#include <stdexcept>
#include <string>
#include <vector>

namespace khdays::assets {

// The file's bytes; std::runtime_error "cannot open <what> file: <path>" (or size, or read it all) when it cannot.
inline std::vector<std::uint8_t> read_file_bytes(const std::filesystem::path& path, const char* what) {
    std::ifstream stream{path, std::ios::binary};
    if (!stream)
        throw std::runtime_error(std::string("cannot open ") + what + " file: " + path.string());
    stream.seekg(0, std::ios::end);
    const auto end = stream.tellg();
    if (end < 0)
        throw std::runtime_error(std::string("cannot size ") + what + " file: " + path.string());
    std::vector<std::uint8_t> data(static_cast<std::size_t>(end));
    stream.seekg(0, std::ios::beg);
    if (!data.empty() && !stream.read(reinterpret_cast<char*>(data.data()), static_cast<std::streamsize>(data.size())))
        throw std::runtime_error(std::string("cannot read complete ") + what + " file: " + path.string());
    return data;
}

}  // namespace khdays::assets
