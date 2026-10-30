// A form's rule (`when`, docs/CHARACTERS.md "Forms") as the Characters window builds it: conditions joined all by
// "and" or all by "or", each one maybe after "not". A rule written otherwise (parentheses, both joins) is kept as
// its text. The port's own parser (port/runtime/characters/rules.c) checks what is written.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace studio {

struct RuleCondition {
    enum Kind { gear, links, panel, dual, level, hp, plugin };
    Kind kind = gear;
    bool negate = false;
    int item = 0;           // gear, links (0: any link), panel
    std::string op = ">=";  // links, panel, level, hp: >= <= > < == !=
    long n = 1;
    std::string name;  // plugin
};

struct RuleRows {
    bool any = false;  // joined by "or" (else "and")
    std::vector<RuleCondition> conditions;
};

// `text` (a comment after " #" left out) as rows; false when it is not one the rows can hold.
bool rule_rows(const std::string& text, RuleRows* out);

// The rows as a rule, and the names of the items it names for its comment ("Zero Gear, Ability Unit").
std::string rule_text(const RuleRows& rows);
std::string rule_names(const RuleRows& rows, const std::function<std::string(int)>& item_name);

// The comparisons the rows offer, in order.
const std::vector<std::string>& rule_comparisons();

}  // namespace studio
