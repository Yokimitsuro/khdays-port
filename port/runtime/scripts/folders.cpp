// The mods' files (folders.h).
#include "folders.h"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path path_of(const char *utf8)
{
    return fs::path(std::u8string(reinterpret_cast<const char8_t *>(utf8)));
}

std::string utf8_of(const fs::path &p)
{
    const std::u8string s = p.generic_u8string();
    return std::string(s.begin(), s.end());
}

} // namespace

extern "C" int khdays_folders_list(const char *path, void (*each)(const char *name, void *context), void *context)
{
    std::error_code e;
    std::vector<std::string> names;
    const fs::path root = path_of(path);
    if (!fs::is_directory(root, e)) {
        return 0;
    }
    for (fs::directory_iterator it(root, e), end; !e && it != end; it.increment(e)) {
        if (it->is_directory(e)) {
            names.push_back(utf8_of(it->path().filename()));
        }
    }
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        each(n.c_str(), context);
    }
    return 1;
}

extern "C" int khdays_folders_list_files(const char *path, void (*each)(const char *name, void *context), void *context)
{
    std::error_code e;
    std::vector<std::string> names;
    const fs::path root = path_of(path);
    if (!fs::is_directory(root, e)) {
        return 0;
    }
    for (fs::directory_iterator it(root, e), end; !e && it != end; it.increment(e)) {
        if (it->is_regular_file(e)) {
            names.push_back(utf8_of(it->path().filename()));
        }
    }
    std::sort(names.begin(), names.end());
    for (const std::string &n : names) {
        each(n.c_str(), context);
    }
    return 1;
}

extern "C" int khdays_folders_files(const char *path, void (*each)(const char *relative, void *context), void *context)
{
    std::error_code e;
    std::vector<std::string> files;
    const fs::path root = path_of(path);
    if (!fs::is_directory(root, e)) {
        return 0;
    }
    for (fs::recursive_directory_iterator it(root, e), end; !e && it != end; it.increment(e)) {
        if (it->is_regular_file(e)) {
            files.push_back(utf8_of(it->path().lexically_relative(root)));
        }
    }
    std::sort(files.begin(), files.end());
    for (const std::string &f : files) {
        each(f.c_str(), context);
    }
    return 1;
}

extern "C" long long khdays_folders_time(const char *path)
{
    std::error_code e;
    const auto t = fs::last_write_time(path_of(path), e);
    return e ? -1 : static_cast<long long>(t.time_since_epoch().count());
}

extern "C" long long khdays_folders_size(const char *path)
{
    std::error_code e;
    const auto n = fs::file_size(path_of(path), e);
    return e ? -1 : static_cast<long long>(n);
}

extern "C" int khdays_folders_is_file(const char *path)
{
    std::error_code e;
    return fs::is_regular_file(path_of(path), e) ? 1 : 0;
}

extern "C" char *khdays_folders_read(const char *path, size_t *size)
{
    std::ifstream in(path_of(path), std::ios::binary);
    if (!in) {
        return nullptr;
    }
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return nullptr;
    }
    char *out = static_cast<char *>(std::malloc(bytes.size() + 1));
    if (out == nullptr) {
        return nullptr;
    }
    if (!bytes.empty()) {
        std::memcpy(out, bytes.data(), bytes.size());
    }
    out[bytes.size()] = 0;
    if (size != nullptr) {
        *size = bytes.size();
    }
    return out;
}

extern "C" int khdays_folders_write(const char *path, const void *data, size_t size)
{
    std::error_code e;
    const fs::path target = path_of(path);
    fs::path temporary = target;
    temporary += u8".writing";
    if (target.has_parent_path()) {
        fs::create_directories(target.parent_path(), e);
    }
    {
        std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
        if (!out) {
            return 0;
        }
        out.write(static_cast<const char *>(data), static_cast<std::streamsize>(size));
        out.flush();
        if (!out) {
            out.close();
            fs::remove(temporary, e);
            return 0;
        }
    }
    fs::rename(temporary, target, e);
    if (e) {
        fs::remove(temporary, e);
        return 0;
    }
    return 1;
}

extern "C" int khdays_folders_rename(const char *from, const char *to)
{
    std::error_code e;
    fs::rename(path_of(from), path_of(to), e);
    return e ? 0 : 1;
}

extern "C" int khdays_folders_remove(const char *path)
{
    std::error_code e;
    return fs::remove(path_of(path), e) ? 1 : 0;
}
