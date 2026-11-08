// Headless tools for KH2 enemies (khdays-native):
//   --kh2-ai-calls                          the calls the port handles, `kind:number` a line (the coverage's list)
//   --kh2-ai-run <mdlx> <calls.txt> <ticks> an object's script run on a made-up actor, its actor watched: the states
//                                           it enters, the calls it makes, the actor's offsets it reads and writes
//                                           itself (docs/KH2_ENEMIES.md)
#include "actor.h"
#include "calls.h"

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace kh = khdays::kh2_enemies;
namespace ai = khdays::assets::kh2::ai;

namespace {

bool read_file(const char *path, std::string &out)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return false;
    std::stringstream s;
    s << in.rdbuf();
    out = s.str();
    return true;
}

int list_calls()
{
    ai::Calls calls;
    kh::add_all(calls);
    for (const auto &[kind, number] : calls.handled())
        std::printf("%u:%u\n", kind, number);
    return 0;
}

int run(const char *mdlx_path, const char *calls_path, int ticks)
{
    std::string bar, shapes;
    if (!read_file(mdlx_path, bar) || !read_file(calls_path, shapes)) {
        std::fprintf(stderr, "kh2 ai run: cannot read %s or %s\n", mdlx_path, calls_path);
        return 1;
    }
    std::vector<std::pair<std::string, ai::Script>> scripts;
    try {
        scripts = ai::scripts_in(reinterpret_cast<const std::uint8_t *>(bar.data()), bar.size());
    } catch (const std::exception &e) {
        std::fprintf(stderr, "kh2 ai run: %s\n", e.what());
        return 1;
    }
    if (scripts.empty()) {
        std::fprintf(stderr, "kh2 ai run: %s has no AI script\n", mdlx_path);
        return 1;
    }
    // every call KH2 has, counted as it is made, then answered by the port's handler when there is one
    ai::Calls real, counted;
    kh::add_all(real);
    if (kh::read_shapes(real, shapes) == 0 || kh::read_shapes(counted, shapes) == 0) {
        std::fprintf(stderr, "kh2 ai run: %s holds no calls' shapes\n", calls_path);
        return 1;
    }
    std::map<std::pair<unsigned, unsigned>, int> made;
    for (const auto &[kind, number] : counted.shaped()) {
        counted.set(kind, number, [&, kind = kind, number = number](ai::Machine &m, std::span<ai::Value> a) {
            ++made[{kind, number}];
            const ai::Calls::Entry *e = real.find(kind, number);
            if (e != nullptr && e->fn)
                e->fn(m, a);
            else
                a[0] = ai::Value{0, ai::Tag::Int};
        });
    }
    ai::Memory memory;
    std::vector<std::string> reports;
    memory.report = [&](const std::string &s) { reports.push_back(s); };
    auto script = std::make_shared<const ai::Script>(scripts[0].second);
    kh::Actor actor(script, memory, counted, script->name);
    actor.watch.on = true;
    std::vector<std::pair<int, std::string>> states;
    int tick = 0;
    actor.states().entered = [&](const std::string &name) { states.emplace_back(tick, name); };
    actor.start();
    for (tick = 1; tick <= ticks; ++tick)
        actor.tick();

    std::printf("kh2 ai run: %s (%s), %d ticks\n", script->name.c_str(), scripts[0].first.c_str(), ticks);
    std::printf("states entered:");
    for (const auto &[t, name] : states)
        std::printf(" %s@%d", name.c_str(), t);
    std::printf("\ncalls made (* not handled):");
    for (const auto &[call, n] : made) {
        const ai::Calls::Entry *e = real.find(call.first, call.second);
        std::printf(" %u:%u%s x%d", call.first, call.second, e != nullptr && e->fn ? "" : "*", n);
    }
    std::printf("\nthe actor read by the script itself:");
    for (const auto &[offset, n] : actor.watch.reads)
        std::printf(" +%#x x%u", offset, n);
    std::printf("\nthe actor written by the script itself:");
    for (const auto &[offset, n] : actor.watch.writes)
        std::printf(" +%#x x%u", offset, n);
    std::printf("\nreports:\n");
    for (const auto &r : reports)
        std::printf("  %s\n", r.c_str());
    return 0;
}

} // namespace

extern "C" int khdays_kh2_ai_tool(int argc, char **argv)
{
    if (argc >= 1 && std::string(argv[0]) == "--kh2-ai-calls")
        return list_calls();
    if (argc >= 4 && std::string(argv[0]) == "--kh2-ai-run")
        return run(argv[1], argv[2], std::atoi(argv[3]));
    std::fprintf(stderr, "usage: --kh2-ai-calls | --kh2-ai-run <mdlx> <calls.txt> <ticks>\n");
    return 2;
}
