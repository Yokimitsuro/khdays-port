#include "flat_json.h"

#include <charconv>
#include <cmath>
#include <fstream>
#include <set>
#include <utility>

namespace khdays::ui::json {
namespace {

class Reader {
public:
    Reader(std::string input, const std::string &what, const Rules &rules)
        : text(std::move(input)), what(what), rules(rules)
    {
    }
    Object read()
    {
        object("", 0);
        if (next() != '\0' || at != text.size())
            fail();
        return values;
    }

private:
    std::string text;
    const std::string &what;
    const Rules &rules;
    std::size_t at = 0;
    Object values;
    std::set<std::string> keys;

    [[noreturn]] void fail() const
    {
        throw std::runtime_error(what + " JSON: invalid object at byte " + std::to_string(at));
    }
    char next()
    {
        while (at < text.size() && (text[at] == ' ' || text[at] == '\n' || text[at] == '\r' || text[at] == '\t'))
            ++at;
        return at < text.size() ? text[at] : '\0';
    }
    void take(char c)
    {
        if (next() != c)
            fail();
        ++at;
    }
    unsigned hex()
    {
        unsigned value = 0;
        for (int i = 0; i < 4; ++i) {
            if (at == text.size())
                fail();
            const char c = text[at++];
            const int n = c >= '0' && c <= '9'   ? c - '0'
                          : c >= 'a' && c <= 'f' ? c - 'a' + 10
                          : c >= 'A' && c <= 'F' ? c - 'A' + 10
                                                 : -1;
            if (n < 0)
                fail();
            value = value * 16 + unsigned(n);
        }
        return value;
    }
    // a \u escape's character (a pair of them for one past the first plane) in UTF-8
    void unicode(std::string &out)
    {
        unsigned v = hex();
        if (v >= 0xd800 && v <= 0xdbff) {
            if (at + 2 > text.size() || text[at++] != '\\' || text[at++] != 'u')
                fail();
            const unsigned low = hex();
            if (low < 0xdc00 || low > 0xdfff)
                fail();
            v = 0x10000 + (v - 0xd800) * 1024 + low - 0xdc00;
        } else if (v >= 0xdc00 && v <= 0xdfff) {
            fail();
        }
        if (v < 0x80) {
            out += char(v);
        } else {
            if (v < 0x800) {
                out += char(0xc0 | (v >> 6));
            } else {
                if (v < 0x10000) {
                    out += char(0xe0 | (v >> 12));
                } else {
                    out += char(0xf0 | (v >> 18));
                    out += char(0x80 | ((v >> 12) & 63));
                }
                out += char(0x80 | ((v >> 6) & 63));
            }
            out += char(0x80 | (v & 63));
        }
    }
    std::string string()
    {
        take('"');
        std::string out;
        while (at < text.size() && text[at] != '"') {
            char c = text[at++];
            if (static_cast<unsigned char>(c) < 32)
                fail();
            if (c == '\\') {
                if (at == text.size())
                    fail();
                c = text[at++];
                switch (c) {
                case '"':
                case '\\':
                case '/': break;
                case 'n': c = '\n'; break;
                case 'r': c = '\r'; break;
                case 't': c = '\t'; break;
                case 'b': c = '\b'; break;
                case 'f': c = '\f'; break;
                case 'u': unicode(out); continue;
                default: fail();
                }
            }
            out += c;
        }
        take('"');
        return out;
    }
    Value scalar()
    {
        if (next() == '"')
            return string();
        for (const auto &[word, value] : {std::pair{"true", true}, std::pair{"false", false}}) {
            const std::string token(word);
            if (text.compare(at, token.size(), token) == 0) {
                at += token.size();
                return value;
            }
        }
        const auto start = at;
        if (next() == '-')
            ++at;
        auto digit = [&] { return at < text.size() && text[at] >= '0' && text[at] <= '9'; };
        if (!digit())
            fail();
        if (text[at] == '0')
            ++at;
        else
            while (digit())
                ++at;
        if (at < text.size() && text[at] == '.') {
            ++at;
            if (!digit())
                fail();
            while (digit())
                ++at;
        }
        if (at < text.size() && (text[at] == 'e' || text[at] == 'E')) {
            ++at;
            if (at < text.size() && (text[at] == '+' || text[at] == '-'))
                ++at;
            if (!digit())
                fail();
            while (digit())
                ++at;
        }
        double n = 0;
        const auto parsed = std::from_chars(text.data() + start, text.data() + at, n);
        if (parsed.ec != std::errc{} || parsed.ptr != text.data() + at || !std::isfinite(n))
            fail();
        return n;
    }
    void object(const std::string &prefix, int depth)
    {
        if (depth > rules.deepest)
            fail();
        take('{');
        if (next() != '}')
            for (;;) {
                const auto key = string();
                if (key.empty() || key.find('.') != std::string::npos)
                    fail();
                const auto name = prefix + key;
                if (!keys.insert(name).second)
                    fail();
                take(':');
                if (next() == '{') {
                    if (!rules.nest(prefix, key, depth))
                        fail();
                    object(name + '.', depth + 1);
                } else {
                    if (!rules.leaf(key, depth))
                        fail();
                    values.emplace(name, scalar());
                }
                if (next() != ',')
                    break;
                ++at;
            }
        take('}');
    }
};

} // namespace

Object read(std::string text, const std::string &what, const Rules &rules)
{
    return Reader(std::move(text), what, rules).read();
}

Object read_file(const std::filesystem::path &path, const std::string &what, const Rules &rules)
{
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() < 0)
        throw std::runtime_error(what + " JSON: cannot read " + path.string());
    std::string text(static_cast<std::size_t>(file.tellg()), '\0');
    file.seekg(0);
    if (!file.read(text.data(), std::streamsize(text.size())))
        throw std::runtime_error(what + " JSON: incomplete file");
    return read(std::move(text), what, rules);
}

double number(Object &o, const std::string &key, double fallback, double lo, double hi, bool integer,
              const std::string &what)
{
    const double n = get(o, key, fallback, what);
    if (n < lo || n > hi || (integer && n != std::floor(n)))
        throw std::runtime_error(what + " JSON: out of range " + key);
    return n;
}

} // namespace khdays::ui::json
