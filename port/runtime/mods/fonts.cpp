#include "fonts.h"
#include "../../host/app/hud_settings.h"

extern "C" {
#include "../../host/app/host.h"
#include "../../host/render/png_io.h"
}
extern "C" int khdays_mods_find(const char *relative, char *path, int size);

#include <charconv>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace khdays::hd_fonts {
namespace {

struct Value {
    enum Type { Null, Boolean, Integer, Real, String, Array, Object } type = Null;
    std::int64_t number = 0;
    double real = 0;
    std::string str;
    std::vector<Value> array;
    std::map<std::string, Value> object;
};

// Strict JSON reader: metrics stay integers; explicit scale may be fractional.
// Unknown optional keys are accepted and recursively read.
class Reader {
public:
    explicit Reader(std::string data) : text_(std::move(data)) {}
    Value read()
    {
        Value value = parse(0);
        skip_space();
        if (pos_ != text_.size())
            fail("trailing content");
        return value;
    }

private:
    std::string text_;
    std::size_t pos_ = 0;
    void fail(const char *why) const
    {
        throw std::runtime_error(std::string(why) + " at JSON byte " + std::to_string(pos_));
    }
    void skip_space()
    {
        while (pos_ < text_.size() &&
               (text_[pos_] == ' ' || text_[pos_] == '\n' || text_[pos_] == '\r' || text_[pos_] == '\t'))
            ++pos_;
    }
    bool take(char ch)
    {
        skip_space();
        if (pos_ < text_.size() && text_[pos_] == ch) {
            ++pos_;
            return true;
        }
        return false;
    }
    void expect(char ch)
    {
        if (!take(ch))
            fail("unexpected JSON character");
    }
    unsigned unicode()
    {
        if (pos_ + 4 > text_.size())
            fail("short Unicode escape");
        unsigned code = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = text_[pos_++];
            if (h >= '0' && h <= '9')
                code = (code << 4) | static_cast<unsigned>(h - '0');
            else if (h >= 'a' && h <= 'f')
                code = (code << 4) | static_cast<unsigned>(h - 'a' + 10);
            else if (h >= 'A' && h <= 'F')
                code = (code << 4) | static_cast<unsigned>(h - 'A' + 10);
            else
                fail("invalid Unicode escape");
        }
        return code;
    }
    std::string string()
    {
        expect('"');
        std::string out;
        while (pos_ < text_.size()) {
            const unsigned char c = static_cast<unsigned char>(text_[pos_++]);
            if (c == '"')
                return out;
            if (c < 0x20)
                fail("control character in JSON string");
            if (c != '\\') {
                out.push_back(static_cast<char>(c));
                continue;
            }
            if (pos_ >= text_.size())
                fail("unterminated JSON escape");
            const char escaped = text_[pos_++];
            switch (escaped) {
            case '"':
            case '\\':
            case '/': out.push_back(escaped); break;
            case 'n': out.push_back('\n'); break;
            case 'r': out.push_back('\r'); break;
            case 't': out.push_back('\t'); break;
            case 'b': out.push_back('\b'); break;
            case 'f': out.push_back('\f'); break;
            case 'u': {
                unsigned code = unicode();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (text_.compare(pos_, 2, "\\u") != 0)
                        fail("missing Unicode low surrogate");
                    pos_ += 2;
                    const unsigned low = unicode();
                    if (low < 0xdc00 || low > 0xdfff)
                        fail("invalid Unicode low surrogate");
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) {
                    fail("unpaired Unicode low surrogate");
                }
                if (code < 0x80)
                    out.push_back(static_cast<char>(code));
                else if (code < 0x800) {
                    out.push_back(static_cast<char>(0xC0 | (code >> 6)));
                    out.push_back(static_cast<char>(0x80 | (code & 63)));
                } else if (code < 0x10000) {
                    out.push_back(static_cast<char>(0xE0 | (code >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
                    out.push_back(static_cast<char>(0x80 | (code & 63)));
                } else {
                    out.push_back(static_cast<char>(0xF0 | (code >> 18)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 12) & 63)));
                    out.push_back(static_cast<char>(0x80 | ((code >> 6) & 63)));
                    out.push_back(static_cast<char>(0x80 | (code & 63)));
                }
                break;
            }
            default: fail("unknown JSON escape");
            }
        }
        fail("unterminated JSON string");
        return {};
    }
    Value parse(int depth)
    {
        if (depth > 32)
            fail("JSON nesting too deep");
        skip_space();
        if (pos_ >= text_.size())
            fail("unexpected end of JSON");
        Value value;
        const char ch = text_[pos_];
        if (ch == '{') {
            ++pos_;
            value.type = Value::Object;
            if (take('}'))
                return value;
            do {
                skip_space();
                if (pos_ >= text_.size() || text_[pos_] != '"')
                    fail("object key must be a string");
                std::string key = string();
                expect(':');
                auto entry = value.object.emplace(std::move(key), parse(depth + 1));
                if (!entry.second)
                    fail("duplicate JSON object key");
                if (take('}'))
                    return value;
                expect(',');
            } while (true);
        }
        if (ch == '[') {
            ++pos_;
            value.type = Value::Array;
            if (take(']'))
                return value;
            do {
                value.array.push_back(parse(depth + 1));
                if (take(']'))
                    return value;
                expect(',');
            } while (true);
        }
        if (ch == '"') {
            value.type = Value::String;
            value.str = string();
            return value;
        }
        if (text_.compare(pos_, 4, "null") == 0) {
            pos_ += 4;
            return value;
        }
        if (text_.compare(pos_, 4, "true") == 0) {
            pos_ += 4;
            value.type = Value::Boolean;
            return value;
        }
        if (text_.compare(pos_, 5, "false") == 0) {
            pos_ += 5;
            value.type = Value::Boolean;
            return value;
        }
        if (ch == '-' || (ch >= '0' && ch <= '9')) {
            const std::size_t begin = pos_;
            if (text_[pos_] == '-')
                ++pos_;
            if (pos_ >= text_.size())
                fail("invalid JSON number");
            if (text_[pos_] == '0')
                ++pos_;
            else {
                if (text_[pos_] < '1' || text_[pos_] > '9')
                    fail("invalid JSON number");
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
                    ++pos_;
            }
            bool fractional = false;
            if (pos_ < text_.size() && text_[pos_] == '.') {
                fractional = true;
                ++pos_;
                const auto digits = pos_;
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
                    ++pos_;
                if (pos_ == digits)
                    fail("invalid JSON fraction");
            }
            if (pos_ < text_.size() && (text_[pos_] == 'e' || text_[pos_] == 'E')) {
                fractional = true;
                ++pos_;
                if (pos_ < text_.size() && (text_[pos_] == '+' || text_[pos_] == '-'))
                    ++pos_;
                const auto digits = pos_;
                while (pos_ < text_.size() && text_[pos_] >= '0' && text_[pos_] <= '9')
                    ++pos_;
                if (pos_ == digits)
                    fail("invalid JSON exponent");
            }
            if (fractional) {
                const auto result = std::from_chars(text_.data() + begin, text_.data() + pos_, value.real);
                if (result.ec != std::errc{} || result.ptr != text_.data() + pos_)
                    fail("JSON number out of range");
                if (!std::isfinite(value.real))
                    fail("nonfinite JSON number");
                value.type = Value::Real;
                return value;
            }
            const auto result = std::from_chars(text_.data() + begin, text_.data() + pos_, value.number);
            if (result.ec != std::errc{} || result.ptr != text_.data() + pos_)
                fail("JSON integer out of range");
            value.type = Value::Integer;
            return value;
        }
        fail("invalid JSON value");
        return {};
    }
};

const Value &field(const Value &obj, const char *name, Value::Type type)
{
    if (obj.type != Value::Object)
        throw std::runtime_error("expected JSON object");
    const auto it = obj.object.find(name);
    if (it == obj.object.end() || it->second.type != type)
        throw std::runtime_error(std::string("missing or invalid field: ") + name);
    return it->second;
}
int integer(const Value &v, const char *name, int lo, int hi)
{
    const auto n = field(v, name, Value::Integer).number;
    if (n < lo || n > hi)
        throw std::runtime_error(std::string("out of range: ") + name);
    return static_cast<int>(n);
}
std::filesystem::path u8(const std::string &path)
{
    return std::filesystem::path(std::u8string(path.begin(), path.end()));
}

std::string text_file(const std::filesystem::path &path)
{
    std::ifstream f(path, std::ios::binary | std::ios::ate);
    if (!f)
        throw std::runtime_error("cannot open metadata JSON");
    const auto size = f.tellg();
    if (size < 0)
        throw std::runtime_error("cannot read metadata JSON");
    std::string data(static_cast<std::size_t>(size), '\0');
    f.seekg(0);
    if (!data.empty() && !f.read(data.data(), size))
        throw std::runtime_error("cannot read metadata JSON");
    return data;
}

std::string resource_path(std::string path)
{
    for (char &ch : path)
        if (ch == '\\')
            ch = '/';
    const auto start = path.find_first_not_of('/');
    if (start == std::string::npos)
        return {};
    path.erase(0, start);
    if (path.size() >= 5) {
        std::string suffix = path.substr(path.size() - 5);
        for (char &ch : suffix)
            if (ch >= 'A' && ch <= 'Z')
                ch = static_cast<char>(ch + ('a' - 'A'));
        if (suffix == ".nftr")
            path.replace(path.size() - 5, 5, suffix);
    }
    return path;
}

std::shared_ptr<const Atlas> read_atlas(const std::string &resource, const std::filesystem::path &png_path,
                                        bool secondary = false)
{
    std::filesystem::path json_path = png_path;
    json_path.replace_extension(".json"); // foo.nftr.png -> foo.nftr.json
    const Value doc = Reader(text_file(json_path)).read();
    const auto &src = field(doc, "source", Value::String).str;
    if (resource_path(src) != resource)
        throw std::runtime_error("JSON source does not match original NFTR path");
    if (field(doc, "format", Value::String).str != "NFTR")
        throw std::runtime_error("JSON is not an NFTR atlas");
    const auto &reference = field(doc, "atlas", Value::Object);
    auto output = std::make_shared<Atlas>();
    output->native_metrics = secondary;
    if (secondary && doc.object.find("metrics") != doc.object.end()) {
        const auto &metrics = field(doc, "metrics", Value::String).str;
        if (metrics != "original" && metrics != "custom")
            throw std::runtime_error("secondary metrics must be \"original\" or \"custom\"");
        output->native_metrics = metrics == "original";
    }
    output->source = resource;
    const auto utf8 = png_path.u8string();
    output->png_path.assign(utf8.begin(), utf8.end());
    output->reference_width = integer(reference, "width", 1, 16384);
    output->reference_height = integer(reference, "height", 1, 16384);
    output->cell_width = integer(doc, "cell_width", 1, 256);
    output->cell_height = integer(doc, "cell_height", 1, 256);
    output->baseline = integer(doc, "baseline", -256, 256);
    if (doc.object.find("baseline_offset") != doc.object.end())
        output->baseline_offset = integer(doc, "baseline_offset", -1024, 1024);
    output->line_feed = integer(doc, "line_feed", 0, 256);
    output->max_width = integer(doc, "max_width", 0, 256);
    const auto sizing = doc.object.find("scale");
    if (sizing != doc.object.end()) {
        const Value &v = sizing->second;
        if (v.type != Value::Integer && v.type != Value::Real)
            throw std::runtime_error("scale must be a number");
        output->scale = v.type == Value::Integer ? static_cast<double>(v.number) : v.real;
        if (!std::isfinite(output->scale) || output->scale < 1.0 / 64 || output->scale > 16)
            throw std::runtime_error("scale must be between 0.015625 and 16");
        output->sized = true;
    }

    const Value &glyphs = field(doc, "glyphs", Value::Array);
    if (glyphs.array.empty())
        throw std::runtime_error("no glyphs");
    output->glyphs.resize(glyphs.array.size());
    std::vector<bool> seen(glyphs.array.size());
    for (const Value &g : glyphs.array) {
        const int id = integer(g, "index", 0, static_cast<int>(glyphs.array.size()) - 1);
        if (seen[id])
            throw std::runtime_error("duplicate glyph index");
        seen[id] = true;
        Glyph &dst = output->glyphs[id];
        if (g.object.find("fallback") != g.object.end()) {
            const auto &fallback = field(g, "fallback", Value::String).str;
            if (fallback == "original")
                dst.original = output->has_original = true;
            else if (fallback == "secondary" && !secondary)
                dst.secondary = output->has_secondary = true;
            else
                throw std::runtime_error(secondary ? "secondary atlases cannot request another secondary fallback"
                                                   : "glyph fallback must be \"original\" or \"secondary\"");
            continue;
        }
        dst.x = integer(g, "x", 0, output->reference_width);
        dst.y = integer(g, "y", 0, output->reference_height);
        dst.w = integer(g, "w", 1, output->reference_width);
        dst.h = integer(g, "h", 1, output->reference_height);
        if (dst.w > output->reference_width - dst.x || dst.h > output->reference_height - dst.y)
            throw std::runtime_error("glyph rectangle exceeds reference atlas");
        dst.left = integer(g, "left", -1024, 1024);
        dst.width = integer(g, "width", 0, 1024);
        dst.advance = integer(g, "advance", -1024, 1024);
    }
    const Value &charmap = field(doc, "charmap", Value::Array);
    std::unordered_map<std::uint32_t, bool> mapped_codes;
    for (const Value &m : charmap.array) {
        const auto code = integer(m, "codepoint", 0, 0x10ffff);
        const auto glyph = integer(m, "glyph", 0, static_cast<int>(glyphs.array.size()) - 1);
        if (!mapped_codes.emplace(static_cast<std::uint32_t>(code), true).second)
            throw std::runtime_error("duplicate charmap codepoint");
        output->charmap.push_back({static_cast<std::uint32_t>(code), static_cast<std::uint32_t>(glyph)});
        if (output->glyphs[glyph].original)
            output->original_codes.insert(static_cast<std::uint32_t>(code));
    }

    const auto native = doc.object.find("original_codepoints");
    if (native != doc.object.end()) {
        if (native->second.type != Value::Array)
            throw std::runtime_error("original_codepoints must be an array of codepoints");
        for (const Value &code : native->second.array) {
            if (code.type != Value::Integer || code.number < 0 || code.number > 0x10ffff ||
                !output->original_codepoints.insert(static_cast<std::uint32_t>(code.number)).second)
                throw std::runtime_error("invalid or duplicate original_codepoints entry");
        }
    }
    const auto pairs = doc.object.find("kerning");
    if (pairs != doc.object.end()) {
        if (pairs->second.type != Value::Array)
            throw std::runtime_error("kerning must be an array of pairs");
        for (const Value &pair : pairs->second.array) {
            const auto left = integer(pair, "left", 0, 0x10ffff);
            const auto right = integer(pair, "right", 0, 0x10ffff);
            const int adjust = integer(pair, "adjust", -1024, 1024);
            const std::uint64_t key = (static_cast<std::uint64_t>(left) << 32) | static_cast<std::uint32_t>(right);
            if (!output->kerning.emplace(key, adjust).second)
                throw std::runtime_error("duplicate kerning pair");
        }
    }

    if (secondary && output->charmap.empty())
        throw std::runtime_error("secondary atlas requires a charmap");
    if (output->native_metrics)
        output->sized = false;
    int iw = 0, ih = 0;
    const char *error = nullptr;
    std::unique_ptr<std::uint8_t, decltype(&khdays_png_free)> rgba(
        khdays_png_read(output->png_path.c_str(), &iw, &ih, &error), &khdays_png_free);
    if (!rgba)
        throw std::runtime_error(std::string("PNG read failed: ") + (error ? error : "unknown"));
    if (iw < 1 || ih < 1 || iw > 16384 || ih > 16384 ||
        static_cast<std::int64_t>(iw) * output->reference_height !=
            static_cast<std::int64_t>(ih) * output->reference_width)
        throw std::runtime_error("PNG dimensions must be a uniform scale of the JSON reference atlas");
    output->width = iw;
    output->height = ih;
    // Keep logical glyph sizes untouched. Only sampling rectangles scale.
    const double sx = static_cast<double>(iw) / output->reference_width;
    const double sy = static_cast<double>(ih) / output->reference_height;
    for (Glyph &g : output->glyphs) {
        g.image_x = g.x * sx;
        g.image_y = g.y * sy;
        g.image_w = g.w * sx;
        g.image_h = g.h * sy;
    }
    const auto byte_count = static_cast<std::size_t>(iw) * static_cast<std::size_t>(ih) * 4;
    output->rgba.assign(rgba.get(), rgba.get() + byte_count);
    return output;
}

struct Entry {
    std::shared_ptr<const Atlas> font;
    bool found = false;
    bool invalid = false;
};
std::mutex cache_mutex;
std::unordered_map<std::string, Entry> cache;
using FontRules = std::map<std::string, std::map<std::string, std::string>>;
FontRules rules;
bool rules_read;
std::map<std::pair<std::string, std::string>, std::string> routed;

bool font_path_valid(const std::string &path)
{
    return !path.empty() && path.size() < 160 && path.front() != '/' && path.find('\\') == std::string::npos &&
           path.find(':') == std::string::npos && path.find("..") == std::string::npos &&
           path.find('@') == std::string::npos &&
           std::none_of(
               path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; }) &&
           path.size() > 5 && path.compare(path.size() - 5, 5, ".nftr") == 0;
}

FontRules read_rules(const Value &doc)
{
    if (integer(doc, "version", 1, 1) != 1)
        throw std::runtime_error("unsupported font mapping version");
    const auto &scopes = field(doc, "scopes", Value::Object).object;
    FontRules result;
    for (const auto &[scope, mappings] : scopes) {
        if (scope.empty() || scope.size() >= 80 || scope.front() == '.' || scope.back() == '.' ||
            scope.find("..") != std::string::npos ||
            scope.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos ||
            mappings.type != Value::Object)
            throw std::runtime_error("invalid font scope: " + scope);
        for (const auto &[source, target] : mappings.object) {
            const auto from = resource_path(source);
            const auto to = resource_path(target.str);
            if (target.type != Value::String || !font_path_valid(from) || !font_path_valid(to))
                throw std::runtime_error("invalid font mapping in scope: " + scope);
            if (!result[scope].emplace(from, to).second)
                throw std::runtime_error("duplicate normalized font path in scope: " + scope);
        }
    }
    return result;
}

struct LayoutRule {
    bool numeric = false;
    double max_width = 0, offset = 0;
};
using LayoutRules = std::map<std::string, LayoutRule>;

double layout_number(const Value &doc, const char *name, double low, double high)
{
    const auto found = doc.object.find(name);
    if (found == doc.object.end() || (found->second.type != Value::Integer && found->second.type != Value::Real))
        throw std::runtime_error(std::string("missing or invalid layout field: ") + name);
    const auto &v = found->second;
    const double result = v.type == Value::Integer ? static_cast<double>(v.number) : v.real;
    if (!std::isfinite(result) || result < low || result > high)
        throw std::runtime_error(std::string("out of range layout field: ") + name);
    return result;
}

LayoutRules read_layouts(const Value &doc)
{
    if (integer(doc, "version", 1, 1) != 1)
        throw std::runtime_error("unsupported font layout version; use version 1");
    const auto &scopes = field(doc, "scopes", Value::Object).object;
    LayoutRules result;
    for (const auto &[scope, mode] : scopes) {
        if (scope.empty() || scope.size() >= 80 || scope.front() == '.' || scope.back() == '.' ||
            scope.find("..") != std::string::npos ||
            scope.find_first_not_of("abcdefghijklmnopqrstuvwxyz0123456789._-") != std::string::npos ||
            (mode.type != Value::String && mode.type != Value::Object))
            throw std::runtime_error("invalid font layout scope: " + scope);
        LayoutRule rule;
        if (mode.type == Value::Object) {
            if (field(mode, "mode", Value::String).str != "fit")
                throw std::runtime_error("invalid layout mode in " + scope + "; use fit");
            rule.max_width = layout_number(mode, "max_width", .25, 64);
            if (mode.object.count("x_offset"))
                rule.offset = layout_number(mode, "x_offset", 0, 64);
            if (rule.offset + rule.max_width > 64)
                throw std::runtime_error("fit region exceeds the 64-pixel item-name strip in " + scope);
        } else {
            if (mode.str != "original" && mode.str != "font_metrics")
                throw std::runtime_error("invalid font layout in " + scope + "; use original or font_metrics");
            rule.numeric = mode.str == "font_metrics";
        }
        result.emplace(scope, rule);
    }
    return result;
}

bool layout_for(const LayoutRules &layouts, std::string scope)
{
    while (!scope.empty()) {
        const auto found = layouts.find(scope);
        if (found != layouts.end())
            return found->second.numeric;
        const auto dot = scope.rfind('.');
        if (dot == std::string::npos)
            break;
        scope.resize(dot);
    }
    return false;
}

void load_rules()
{
    if (rules_read)
        return;
    rules_read = true;
    char path[4096]{};
    if (!khdays_mods_find("fonts/mappings.json", path, sizeof(path)))
        return;
    try {
        rules = read_rules(Reader(text_file(u8(path))).read());
        std::fprintf(stderr, "HD font mappings: %s (%zu scopes)\n", path, rules.size());
    } catch (const std::exception &e) {
        std::fprintf(stderr, "HD font mappings: rejected %s: %s\n", path, e.what());
    }
}

std::string target_for(const std::string &original, std::string scope)
{
    while (!scope.empty()) {
        const auto group = rules.find(scope);
        if (group != rules.end()) {
            const auto entry = group->second.find(original);
            if (entry != group->second.end())
                return entry->second;
        }
        const auto dot = scope.rfind('.');
        if (dot == std::string::npos)
            break;
        scope.resize(dot);
    }
    return {};
}

std::shared_ptr<Atlas> routed_view(const std::shared_ptr<const Atlas> &base)
{
    auto view = std::make_shared<Atlas>();
    view->source = base->source;
    view->png_path = base->png_path;
    view->width = base->width;
    view->height = base->height;
    view->reference_width = base->reference_width;
    view->reference_height = base->reference_height;
    view->cell_width = base->cell_width;
    view->cell_height = base->cell_height;
    view->baseline = base->baseline;
    view->line_feed = base->line_feed;
    view->baseline_offset = base->baseline_offset;
    view->max_width = base->max_width;
    view->sized = base->sized;
    view->scale = base->scale;
    view->glyphs = base->glyphs;
    view->charmap = base->charmap;
    view->original_codepoints = base->original_codepoints;
    view->has_original = base->has_original;
    view->has_secondary = base->has_secondary;
    view->native_metrics = base->native_metrics;
    view->primary_missing = base->primary_missing;
    view->secondary = base->secondary;
    view->kerning = base->kerning;
    view->artwork = base->artwork ? base->artwork : base;
    return view;
}

Entry load_pair(const std::string &original, bool secondary)
{
    // Never let a mod-supplied path escape the fonts subtree.
    if (!font_path_valid(original))
        return {};
    const std::string relative = "fonts/" + original + (secondary ? ".fallback.png" : ".png");
    char path[4096]{};
    if (!khdays_mods_find(relative.c_str(), path, sizeof(path)))
        return {};
    try {
        auto font = read_atlas(original, u8(path), secondary);
        std::fprintf(stderr, "HD fonts: %s <- %s (%dx%d from %dx%d, %zu glyphs)\n", original.c_str(), path, font->width,
                     font->height, font->reference_width, font->reference_height, font->glyphs.size());
        return {std::move(font), true, false};
    } catch (const std::exception &e) {
        std::fprintf(stderr, "HD fonts: rejected %s: %s (continuing font fallback)\n", path, e.what());
        return {nullptr, true, true};
    }
}

Entry load_uncached(const std::string &original)
{
    Entry primary = load_pair(original, false);
    const Entry secondary = load_pair(original, true);
    if (!secondary.font)
        return primary;
    auto combined = routed_view(primary.font ? primary.font : secondary.font);
    if (!primary.font) {
        combined->primary_missing = true;
        combined->glyphs.clear();
        combined->charmap.clear();
        combined->has_original = combined->has_secondary = false;
    }
    combined->secondary = secondary.font;
    return {std::move(combined), true, false};
}
} // namespace

static thread_local bool observing;
static thread_local int layout_mode = -1;
int layout_override(int mode)
{
    const int previous = layout_mode;
    layout_mode = mode;
    return previous;
}
static bool font_enabled()
{
    return layout_mode >= 0 ? layout_mode != 0 : observing || khdays_hud_setting(KHDAYS_OPT_FONT);
}
bool observe(bool enabled)
{
    const bool previous = observing;
    observing = enabled;
    return previous;
}

std::shared_ptr<const Atlas> load(const std::string &original_resource)
{
    if (!font_enabled())
        return nullptr;
    return load_configured(original_resource);
}
std::shared_ptr<const Atlas> load_configured(const std::string &original_resource)
{
    const std::string resource = resource_path(original_resource);
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto [it, inserted] = cache.try_emplace(resource);
    if (inserted)
        it->second = load_uncached(resource);
    return it->second.font;
}
int probe(const std::string &original_resource)
{
    if (!font_enabled())
        return 0;
    const std::string resource = resource_path(original_resource);
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto [it, inserted] = cache.try_emplace(resource);
    if (inserted)
        it->second = load_uncached(resource);
    return !it->second.found ? 0 : it->second.invalid ? -1 : 1;
}

std::string select(const std::string &original_resource, const std::string &scope)
{
    if (!font_enabled())
        return {};
    const auto original = resource_path(original_resource);
    std::string target;
    {
        std::lock_guard<std::mutex> lock(cache_mutex);
        load_rules();
        target = target_for(original, scope);
        if (target.empty() || target == original)
            return {}; // Explicit identity mapping also cancels an inherited rule.
        const auto found = routed.find(std::make_pair(original, target));
        if (found != routed.end())
            return found->second;
        if (routed.size() >= 256)
            return {};
    }
    const auto base = load(target);
    const auto requested = base ? load(original) : nullptr;
    std::lock_guard<std::mutex> lock(cache_mutex);
    const auto pair = std::make_pair(original, target);
    const auto found = routed.find(pair);
    if (found != routed.end())
        return found->second;
    if (routed.size() >= 256)
        return {};
    std::string key;
    if (base && (!base->charmap.empty() || (base->primary_missing && base->secondary))) {
        key = "@font-route/" + std::to_string(routed.size());
        auto view = routed_view(base);
        // The requested NFTR defines symbol meanings; the target's identically coded letters may differ.
        view->original_codepoints = requested ? requested->original_codepoints : std::unordered_set<std::uint32_t>{};
        if (!view->secondary && requested)
            view->secondary = requested->secondary;
        cache.emplace(key, Entry{std::move(view), true, false});
        std::fprintf(stderr, "HD font mapping: %s [%s] -> %s\n", original.c_str(), scope.c_str(), target.c_str());
    } else {
        std::fprintf(stderr,
                     "HD font mapping: %s [%s] -> %s unavailable or missing charmap; keeping original binding\n",
                     original.c_str(), scope.c_str(), target.c_str());
    }
    routed.emplace(pair, key);
    return key;
}

bool mappings_enabled()
{
    if (!font_enabled())
        return false;
    std::lock_guard<std::mutex> lock(cache_mutex);
    load_rules();
    return !rules.empty();
}

const LayoutRules &cached_layouts()
{
    static const LayoutRules cached = [] {
        LayoutRules layouts;
        char path[4096]{};
        if (khdays_mods_find("fonts/layouts.json", path, sizeof(path))) {
            try {
                layouts = read_layouts(Reader(text_file(u8(path))).read());
                std::fprintf(stderr, "HD font layouts: %s (%zu scopes)\n", path, layouts.size());
            } catch (const std::exception &e) {
                std::fprintf(stderr, "HD font layouts: rejected %s: %s\n", path, e.what());
            }
        }
        return layouts;
    }();
    return cached;
}

bool numeric_layout(int layout)
{
    static const std::array<bool, 3> enabled = {layout_for(cached_layouts(), "camp.panels.inventory.counter"),
                                                layout_for(cached_layouts(), "title.load.play_time"),
                                                layout_for(cached_layouts(), "camp.save.play_time")};
    return font_enabled() && layout >= 0 && static_cast<std::size_t>(layout) < enabled.size() && enabled[layout];
}

double fit_layout(int layout, double *offset)
{
    static const std::array<LayoutRule, 2> fitting = [] {
        std::array<LayoutRule, 2> result{};
        const char *scopes[] = {"field.command.items.name", "field.obtained.name"};
        for (std::size_t i = 0; i < result.size(); ++i) {
            const auto found = cached_layouts().find(scopes[i]);
            if (found != cached_layouts().end())
                result[i] = found->second;
        }
        return result;
    }();
    if (offset)
        *offset = 0;
    if (!font_enabled() || layout < 0 || static_cast<std::size_t>(layout) >= fitting.size())
        return 0;
    if (offset)
        *offset = fitting[layout].offset;
    return fitting[layout].max_width;
}
} // namespace khdays::hd_fonts
