// A form's rule as the Characters window builds it (form_rules.h).
#include "core/characters/form_rules.h"

#include <cctype>
#include <sstream>

namespace studio {

namespace {

std::vector<std::string> words_of(const std::string& text) {
    std::vector<std::string> words;
    std::string w;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const char c = text[i];
        if (c == '#' && (i == 0 || std::isspace(static_cast<unsigned char>(text[i - 1]))))
            break;  // its comment
        if (c == '(' || c == ')') {
            if (!w.empty())
                words.push_back(w);
            w.clear();
            words.push_back(std::string(1, c));
        } else if (std::isspace(static_cast<unsigned char>(c))) {
            if (!w.empty())
                words.push_back(w);
            w.clear();
        } else {
            w += c;
        }
    }
    if (!w.empty())
        words.push_back(w);
    return words;
}

bool is_number(const std::string& w, long* out) {
    if (w.empty())
        return false;
    for (const char c : w)
        if (!std::isdigit(static_cast<unsigned char>(c)))
            return false;
    *out = std::stol(w);
    return true;
}

bool is_comparison(const std::string& w) {
    for (const auto& op : rule_comparisons())
        if (w == op)
            return true;
    return false;
}

}  // namespace

const std::vector<std::string>& rule_comparisons() {
    static const std::vector<std::string> ops = {">=", "<=", ">", "<", "==", "!="};
    return ops;
}

bool rule_rows(const std::string& text, RuleRows* out) {
    const std::vector<std::string> w = words_of(text);
    RuleRows rows;
    std::string join;
    for (std::size_t i = 0; i < w.size();) {
        RuleCondition c;
        if (w[i] == "(" || w[i] == ")")
            return false;
        if (w[i] == "not") {
            c.negate = true;
            if (++i >= w.size())
                return false;
        }
        const std::string& k = w[i++];
        long v = 0;
        if (k == "dual") {
            c.kind = RuleCondition::dual;
        } else if (k == "gear" && i < w.size() && is_number(w[i], &v)) {
            c.kind = RuleCondition::gear;
            c.item = static_cast<int>(v);
            ++i;
        } else if (k == "links" || k == "panel" || k == "level" || k == "hp") {
            c.kind = k == "links" ? RuleCondition::links : k == "panel" ? RuleCondition::panel
                     : k == "level"                     ? RuleCondition::level
                                                        : RuleCondition::hp;
            if ((c.kind == RuleCondition::links || c.kind == RuleCondition::panel) && i < w.size() &&
                is_number(w[i], &v)) {
                c.item = static_cast<int>(v);
                ++i;
            }
            if (i < w.size() && is_comparison(w[i])) {
                c.op = w[i];
                if (i + 1 >= w.size() || !is_number(w[i + 1], &c.n))
                    return false;
                i += 2;
            } else if (c.kind != RuleCondition::panel) {
                return false;
            }
        } else if (k == "plugin" && i < w.size()) {
            c.kind = RuleCondition::plugin;
            c.name = w[i++];
        } else {
            return false;
        }
        rows.conditions.push_back(c);
        if (i < w.size()) {
            if (w[i] != "and" && w[i] != "or")
                return false;
            if (!join.empty() && join != w[i])
                return false;  // both joins: precedence the rows cannot show
            join = w[i++];
            if (i >= w.size())
                return false;
        }
    }
    if (rows.conditions.empty())
        return false;
    rows.any = join == "or";
    *out = rows;
    return true;
}

std::string rule_text(const RuleRows& rows) {
    std::ostringstream s;
    for (std::size_t i = 0; i < rows.conditions.size(); ++i) {
        const RuleCondition& c = rows.conditions[i];
        if (i > 0)
            s << (rows.any ? " or " : " and ");
        if (c.negate)
            s << "not ";
        switch (c.kind) {
        case RuleCondition::dual:
            s << "dual";
            break;
        case RuleCondition::gear:
            s << "gear " << c.item;
            break;
        case RuleCondition::links:
            s << "links " << (c.item != 0 ? std::to_string(c.item) + " " : std::string()) << c.op << " " << c.n;
            break;
        case RuleCondition::panel:
            s << "panel " << c.item << " " << c.op << " " << c.n;
            break;
        case RuleCondition::level:
            s << "level " << c.op << " " << c.n;
            break;
        case RuleCondition::hp:
            s << "hp " << c.op << " " << c.n;
            break;
        case RuleCondition::plugin:
            s << "plugin " << c.name;
            break;
        }
    }
    return s.str();
}

std::string rule_names(const RuleRows& rows, const std::function<std::string(int)>& item_name) {
    std::string out;
    for (const RuleCondition& c : rows.conditions) {
        if (c.item == 0 ||
            (c.kind != RuleCondition::gear && c.kind != RuleCondition::links && c.kind != RuleCondition::panel))
            continue;
        const std::string n = item_name(c.item);
        if (!n.empty() && n.find('#') == std::string::npos)
            out += (out.empty() ? "" : ", ") + n;
    }
    return out;
}

}  // namespace studio
