#pragma once

// KINGDOM HEARTS II's AI scripts (an object's .mdlx, BAR entries of type 3) run as KH2FM.exe's interpreter
// (FUN_14041b400) runs them: threads, the tagged value stack, frames and the shared work area in a 32-bit address
// space of the machine's own, and calls into the game through a table the user fills (docs/FORMATS.md, "KH2's
// objects and their AI"; docs/KH2_ENEMIES.md). Our own reading of the player's executable; no game data here.

#include <cstddef>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace khdays::assets::kh2::ai {

// ------------------------------------------------------------------------------------------------- the script

struct Trigger final {
    std::int32_t key = 0;   // 0 start, 1 main, the others events
    std::int32_t entry = 0; // the code word it starts at
};

struct Script final {
    std::string name;
    std::int32_t work = 0, frame = 0, stack = 0; // the work area's, a frame's and the value stack's sizes in bytes
    std::vector<Trigger> triggers;
    std::vector<std::uint8_t> data; // the whole entry: code word n at +0x10 + 2n

    std::size_t words() const { return data.size() < 0x10 ? 0 : (data.size() - 0x10) / 2; }
    std::uint16_t word(std::size_t n) const;
    const Trigger *trigger(std::int32_t key) const;
};

// A script's header and triggers; throws std::runtime_error naming what breaks the layout.
Script read_script(const std::uint8_t *data, std::size_t size);
// A BAR's AI scripts (entries of type 3) with their entry names.
std::vector<std::pair<std::string, Script>> scripts_in(const std::uint8_t *bar, std::size_t size);

// ------------------------------------------------------------------------------------------------- values

enum class Tag : std::uint32_t { Int = 0x544e4940, Float = 0x544c4640, Address = 0x52444140, Unset = 0x3f3f3f40 };

struct Value final {
    std::uint32_t bits = 0;
    Tag tag = Tag::Unset;

    std::int32_t i() const { return static_cast<std::int32_t>(bits); }
    float f() const;
    static Value of_int(std::int32_t v) { return {static_cast<std::uint32_t>(v), Tag::Int}; }
    static Value of_float(float v);
    static Value of_address(std::uint32_t a) { return {a, Tag::Address}; }
};

// ------------------------------------------------------------------------------------------------- memory

// Memory the user keeps itself, seen at a range of the machine's addresses (an actor's fields).
class View {
public:
    virtual ~View() = default;
    virtual void read(std::uint32_t offset, void *out, std::size_t n) = 0;
    virtual void write(std::uint32_t offset, const void *in, std::size_t n) = 0;
};

// The machine's 32-bit address space: regions of bytes (a tag kept for each 4-byte slot of a tagged one, as KH2 keeps
// them for the work area and the frames) and views. Address 0 and every address outside a region read as 0 and write
// nothing; each such miss is reported once by what it was near.
class Memory final {
public:
    std::function<void(const std::string &)> report; // where misses are told (none: kept silent)

    std::uint32_t add(std::uint32_t size, std::string what, bool tagged = false); // zeroed; returns its base
    std::uint32_t add_view(View &view, std::uint32_t size, std::string what);
    void remove(std::uint32_t base);

    bool read(std::uint32_t at, void *out, std::size_t n);
    bool write(std::uint32_t at, const void *in, std::size_t n);
    std::uint32_t u32(std::uint32_t at);
    void set_u32(std::uint32_t at, std::uint32_t v);
    Tag tag(std::uint32_t at) const;                      // Unset outside a tagged region
    bool tagged(std::uint32_t at) const;                  // inside a tagged region's values
    void set_tag(std::uint32_t at, Tag tag);
    void fill_tags(std::uint32_t at, std::size_t slots, Tag tag);
    std::size_t regions() const { return regions_.size(); }

private:
    struct Region {
        std::uint32_t size = 0;
        std::string what;
        std::vector<std::uint8_t> bytes;
        std::vector<Tag> tags; // one a slot; empty: untagged
        View *view = nullptr;
    };
    Region *find(std::uint32_t at, std::size_t n, std::uint32_t &offset);
    const Region *find(std::uint32_t at, std::size_t n, std::uint32_t &offset) const;
    void miss(std::uint32_t at, const char *how);

    std::map<std::uint32_t, Region> regions_;
    std::uint32_t next_ = 0x00100000;
    std::set<std::string> told_;
};

// ------------------------------------------------------------------------------------------------- calls

class Machine;

// A call into the game: its arguments in the order they were pushed; one that gives a value back writes it into
// args[0].
using Call = std::function<void(Machine &, std::span<Value> args)>;

// The calls a script can make: each kind's entries as KH2's tables give them (how many arguments, whether a value
// comes back -- read from the player's executable, see the mod) and the handlers the user has written.
class Calls final {
public:
    void set_shape(unsigned kind, unsigned number, unsigned args, bool returns);
    void set(unsigned kind, unsigned number, Call fn);
    struct Entry {
        unsigned args = 0;
        bool returns = false;
        bool shaped = false; // its arguments' count known
        Call fn;
    };
    const Entry *find(unsigned kind, unsigned number) const;
    std::vector<std::pair<unsigned, unsigned>> handled() const;
    std::vector<std::pair<unsigned, unsigned>> shaped() const;

private:
    std::map<std::uint32_t, Entry> entries_;
};

// ------------------------------------------------------------------------------------------------- the machine

enum class Status { Running, Yielded, Done, Stopped };

struct Thread final {
    std::uint32_t pc = 0;          // the next code word
    std::uint32_t frame_base = 0;  // its frame region
    std::uint32_t fp = 0;          // the frame pointer (an address in its frame region)
    std::vector<Value> stack;      // the value stack
    Status status = Status::Running;
    Thread *parent = nullptr;
    std::vector<std::unique_ptr<Thread>> children;
    std::uint32_t id = 0;
};

// One object's script running: its work area, its main thread and their children.
class Machine final {
public:
    Machine(std::shared_ptr<const Script> script, Memory &memory, const Calls &calls, void *user = nullptr);
    ~Machine();
    Machine(const Machine &) = delete;
    Machine &operator=(const Machine &) = delete;

    const Script &script() const { return *script_; }
    Memory &memory() { return memory_; }
    void *user() const { return user_; }
    std::uint32_t work_area() const { return work_; }
    std::uint32_t code() const { return code_; } // the address of the script's byte 0
    Thread *current() const { return current_; }  // the thread running now (within a call)

    // A trigger as an event: a thread of its own runs its code at once, until its first yield or its end, and is
    // dropped (FUN_1403e1840). Nothing when the script has no such trigger. Returns whether it ran.
    bool raise(std::int32_t trigger, std::span<const Value> args = {});
    // A routine run at once on a thread of its own, until its first yield or its end, then dropped (FUN_1403e2000:
    // a state's routines on entering and leaving). Returns whether it ran.
    bool run_now(std::uint32_t entry, std::span<const Value> args = {});
    // The main thread: the trigger's code, resumed every tick (trigger 1). Replaces a main thread already running.
    Thread *start_main(std::int32_t trigger, std::span<const Value> args = {});
    // A routine as a child thread of `parent` (the main thread when null): a state's routine, a call's thread.
    Thread *start_routine(Thread *parent, std::uint32_t entry, std::span<const Value> args = {});
    void stop(Thread *thread); // ends it and its children at the next tick
    // Every thread once: the main thread, then each child after its parent; ended ones dropped.
    void tick();
    Thread *main_thread() const { return main_.get(); }

    std::uint64_t budget = 100000; // instructions a thread may run without yielding before it is stopped
    float tick_length = 1.0F;      // KH2's 0:3; below 2.0 the PC's 60-frames constant patches apply
    std::uint32_t shared_vector = 0; // where the calls give vectors back (one PC patch reads its w), 0 none

private:
    std::unique_ptr<Thread> make_thread(std::uint32_t entry, std::span<const Value> args);
    void drop_thread(Thread &t);
    Status run(Thread &t);
    void tick_tree(Thread &t);
    void sweep(Thread &t);
    void fault(Thread &t, const std::string &why);

    std::shared_ptr<const Script> script_;
    Memory &memory_;
    const Calls &calls_;
    void *user_;
    std::uint32_t code_ = 0, work_ = 0;
    std::unique_ptr<Thread> main_;
    Thread *current_ = nullptr;
    std::uint32_t next_id_ = 1;
    std::set<std::string> told_;
};

} // namespace khdays::assets::kh2::ai
