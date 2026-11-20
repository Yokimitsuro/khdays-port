// A JSON settings file read flat: its leaves (strings, numbers, true and false) by their dotted keys
// ("party.slots.1.x"), each nested object one its rules allow -- no arrays, no null, no key twice. One reader for the
// HUD's settings files (the gauges', the portraits', the minimap's), with no JSON library.
#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <variant>

namespace khdays::ui::json {

using Value = std::variant<std::string, double, bool>;
using Object = std::map<std::string, Value>;

struct Rules {
    int deepest = 0; // the deepest object (0 the file's own)
    // whether an object may open at `key`, under `prefix` (its parents' keys and dots, "" in the file's own), in an
    // object `depth` deep
    std::function<bool(const std::string &prefix, const std::string &key, int depth)> nest;
    // whether a leaf may stand at `key` in an object `depth` deep
    std::function<bool(const std::string &key, int depth)> leaf;
};

// The file's text read; std::runtime_error "<what> JSON: invalid object at byte N" when it breaks a rule or JSON's.
Object read(std::string text, const std::string &what, const Rules &rules);
// The same, of a file: "<what> JSON: cannot read <path>" when it cannot be read.
Object read_file(const std::filesystem::path &path, const std::string &what, const Rules &rules);

// A leaf taken out of `o` (what is left are keys none asked for), `fallback` when it has none; "<what> JSON: wrong
// type for <key>" when it is not a T.
template <class T> T get(Object &o, const std::string &key, T fallback, const std::string &what)
{
    const auto it = o.find(key);
    if (it == o.end())
        return fallback;
    const auto *v = std::get_if<T>(&it->second);
    if (!v)
        throw std::runtime_error(what + " JSON: wrong type for " + key);
    T result = *v;
    o.erase(it);
    return result;
}

// A number taken out of `o` between `lo` and `hi` (and whole when `integer`); "<what> JSON: out of range <key>".
double number(Object &o, const std::string &key, double fallback, double lo, double hi, bool integer,
              const std::string &what);

} // namespace khdays::ui::json
