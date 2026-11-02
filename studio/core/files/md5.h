// MD5 (RFC 1321): the collections' packages key each file by the MD5 of its path (installer/collection_archive.py).
#pragma once

#include <array>
#include <cstdint>
#include <string_view>

namespace studio {

std::array<std::uint8_t, 16> md5(std::string_view text);

}  // namespace studio
