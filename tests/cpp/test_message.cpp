#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "khdays/assets/message.h"

namespace {

using Bytes = std::vector<std::uint8_t>;

void put_u16(Bytes& b, std::uint16_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    b.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
}

void put_u32(Bytes& b, std::uint32_t v) {
    b.push_back(static_cast<std::uint8_t>(v & 0xFFU));
    b.push_back(static_cast<std::uint8_t>((v >> 8U) & 0xFFU));
    b.push_back(static_cast<std::uint8_t>((v >> 16U) & 0xFFU));
    b.push_back(static_cast<std::uint8_t>((v >> 24U) & 0xFFU));
}

// Build a sub-database blob {u32 count; u32 offsets[count]; u16 data[]} from a
// list of ASCII strings (each stored as UTF-16LE plus a NUL terminator).
Bytes build_blob(const std::vector<std::string>& strings) {
    Bytes data;
    std::vector<std::uint32_t> offsets;
    for (const auto& s : strings) {
        for (char c : s) {
            put_u16(data, static_cast<std::uint16_t>(c));
        }
        put_u16(data, 0U);  // terminator
        offsets.push_back(static_cast<std::uint32_t>(data.size()));
    }
    Bytes blob;
    put_u32(blob, static_cast<std::uint32_t>(strings.size()));
    for (auto o : offsets) {
        put_u32(blob, o);
    }
    blob.insert(blob.end(), data.begin(), data.end());
    return blob;
}

// Wrap bytes in an all-literal LZ11 stream (valid input for lz_decompress).
Bytes lz11_store(const Bytes& data) {
    Bytes out;
    out.push_back(0x11U);
    out.push_back(static_cast<std::uint8_t>(data.size() & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((data.size() >> 8U) & 0xFFU));
    out.push_back(static_cast<std::uint8_t>((data.size() >> 16U) & 0xFFU));
    for (std::size_t i = 0U; i < data.size(); i += 8U) {
        out.push_back(0x00U);  // flag: next 8 are literals
        for (std::size_t j = i; j < i + 8U && j < data.size(); ++j) {
            out.push_back(data[j]);
        }
    }
    return out;
}

void expect(bool ok, const char* what) {
    if (!ok) {
        throw std::runtime_error(what);
    }
}

}  // namespace

int main() {
    const auto path =
        std::filesystem::temp_directory_path() / "khdays_message_test.p2";
    try {
        // Two sub-files: one raw, one LZ11-compressed.
        const Bytes blob0 = build_blob({"Hi", "Bye"});
        const Bytes blob1 = lz11_store(build_blob({"A", "BB", "Ccc"}));

        constexpr std::size_t base = 0x200U;  // data-region base
        constexpr std::size_t sector = 0x200U;

        Bytes file;
        put_u16(file, 0x3250U);  // 'P2'
        put_u16(file, 0x0002U);  // count = 2, no wide flag
        put_u32(file, 0U);
        put_u32(file, 0U);
        put_u32(file, static_cast<std::uint32_t>(base));  // base_offset @0x0C
        // sizes[] @0x10: two u16 (start sectors 0 and 1)
        put_u16(file, 0U);
        put_u16(file, 1U);
        // desc[] @0x14: size + compression flag
        put_u32(file, static_cast<std::uint32_t>(blob0.size()));  // raw
        put_u32(file, 0x80000000U | static_cast<std::uint32_t>(blob1.size()));

        // Sub-file 0 at base + 0*sector, sub-file 1 at base + 1*sector.
        file.resize(base, 0U);
        file.insert(file.end(), blob0.begin(), blob0.end());
        file.resize(base + sector, 0U);
        file.insert(file.end(), blob1.begin(), blob1.end());

        {
            std::ofstream stream{path, std::ios::binary};
            stream.write(
                reinterpret_cast<const char*>(file.data()),
                static_cast<std::streamsize>(file.size()));
        }

        const auto archive = khdays::assets::load_p2_archive(path);
        expect(archive.subdbs.size() == 2U, "sub-database count");
        expect(archive.string_count() == 5U, "total string count");

        expect(archive.subdbs[0].size() == 2U, "sub-db 0 size");
        expect(archive.subdbs[0][0] == u"Hi", "sub-db 0 string 0");
        expect(archive.subdbs[0][1] == u"Bye", "sub-db 0 string 1");

        expect(archive.subdbs[1].size() == 3U, "sub-db 1 size");
        expect(archive.subdbs[1][0] == u"A", "sub-db 1 string 0 (LZ11)");
        expect(archive.subdbs[1][1] == u"BB", "sub-db 1 string 1 (LZ11)");
        expect(archive.subdbs[1][2] == u"Ccc", "sub-db 1 string 2 (LZ11)");

        expect(
            khdays::assets::message_to_utf8(u"A\nB") == "A\nB",
            "utf8 newline passthrough");

        // A UI string table (.s): header {u32 =8; u32 count}, then records
        // {u32 record_length incl. field; u16 utf16le[]}. Shipped LZ11-packed.
        {
            Bytes s;
            put_u32(s, 8U);   // header size
            put_u32(s, 2U);   // count
            for (const std::string str : {std::string{"Nv."}, std::string{"OK"}}) {
                Bytes rec;
                for (char c : str) {
                    put_u16(rec, static_cast<std::uint16_t>(c));
                }
                put_u16(rec, 0U);  // terminator
                put_u32(s, static_cast<std::uint32_t>(rec.size() + 4U));
                s.insert(s.end(), rec.begin(), rec.end());
            }
            const Bytes packed = lz11_store(s);
            const auto spath =
                std::filesystem::temp_directory_path() / "khdays_str_test.s.z";
            {
                std::ofstream stream{spath, std::ios::binary};
                stream.write(
                    reinterpret_cast<const char*>(packed.data()),
                    static_cast<std::streamsize>(packed.size()));
            }
            const auto table = khdays::assets::load_string_table(spath);
            std::filesystem::remove(spath);
            expect(table.size() == 2U, "string table size");
            expect(table[0] == u"Nv.", "string table entry 0");
            expect(table[1] == u"OK", "string table entry 1");
        }

        // Text override escapes: message_from_utf8 decodes what
        // message_to_utf8 emits, and both round-trip control codes/newlines.
        expect(
            khdays::assets::message_from_utf8("Piro\\nPlus") == u"Piro\nPlus",
            "from_utf8 newline escape");
        expect(
            khdays::assets::message_from_utf8("A\\x01B")
                == std::u16string{u'A', u'\x01', u'B'},
            "from_utf8 hex escape");
        {
            const std::u16string original{u'H', u'i', u'\n', u'\x03', u'!'};
            const auto round =
                khdays::assets::message_from_utf8(
                    khdays::assets::message_to_utf8(original));
            expect(round == original, "utf8 round-trip");
        }

        std::filesystem::remove(path);
        std::cout << "Message container test passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::filesystem::remove(path);
        std::cerr << "Message container test failed: " << error.what() << '\n';
        return 1;
    }
}
