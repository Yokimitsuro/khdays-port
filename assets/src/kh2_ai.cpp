// KINGDOM HEARTS II's AI scripts run as KH2FM.exe runs them (kh2_ai.h): the interpreter FUN_14041b400, a thread's
// start FUN_14041b320, its tick FUN_1403e1c80, an event FUN_1403e1840. docs/KH2_ENEMIES.md, "The script machine as
// KH2 runs it", says what each operation does; each piece below names the function it follows.
#include "khdays/assets/kh2_ai.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <stdexcept>

namespace khdays::assets::kh2::ai {
namespace {

constexpr std::size_t kHeader = 0x1c; // the name and the three sizes; the triggers follow
constexpr std::size_t kCode = 0x10;   // code word 0
constexpr std::uint32_t kGap = 0x10;  // between two regions, so that running off one never lands in the next

// KH2's constants for the angle operations (DAT_140623b78, DAT_140623ba4, DAT_140623ab0, DAT_140623bec)
constexpr float kPi = 3.14159274F;
constexpr float kTwoPi = 6.28318548F;
constexpr float kHalfPi = 1.57079637F;
constexpr float kDegrees = 180.0F;

std::uint32_t rd32(const std::uint8_t *p)
{
    std::uint32_t v;
    std::memcpy(&v, p, 4);
    return v;
}

float as_float(std::uint32_t bits)
{
    float f;
    std::memcpy(&f, &bits, 4);
    return f;
}

std::uint32_t bits_of(float f)
{
    std::uint32_t b;
    std::memcpy(&b, &f, 4);
    return b;
}

// FUN_1403b4d30: the PC's curve for a few constants at 60 frames a second
float sixty_curve(float v)
{
    const float root = std::sqrt(v * 8.0F + 1.0F);
    return (root - 1.0F) * 0.5F * 0.35F + ((v + v) / (v + 1.0F)) * 0.65F;
}

} // namespace

// ------------------------------------------------------------------------------------------------- the script

std::uint16_t Script::word(std::size_t n) const
{
    const std::size_t at = kCode + 2 * n;
    if (at + 2 > data.size())
        return 0;
    return static_cast<std::uint16_t>(data[at] | (data[at + 1] << 8));
}

const Trigger *Script::trigger(std::int32_t key) const
{
    for (const auto &t : triggers)
        if (t.key == key)
            return &t;
    return nullptr;
}

Script read_script(const std::uint8_t *data, std::size_t size)
{
    if (size < kHeader)
        throw std::runtime_error("KH2 AI: shorter than its header");
    Script s;
    std::size_t n = 0;
    while (n < 16 && data[n] != 0)
        ++n;
    s.name.assign(reinterpret_cast<const char *>(data), n);
    s.work = static_cast<std::int32_t>(rd32(data + 0x10));
    s.frame = static_cast<std::int32_t>(rd32(data + 0x14));
    s.stack = static_cast<std::int32_t>(rd32(data + 0x18));
    s.data.assign(data, data + size);
    for (std::size_t at = kHeader;; at += 8) {
        if (at + 8 > size)
            throw std::runtime_error("KH2 AI: its triggers run past its end");
        const auto key = static_cast<std::int32_t>(rd32(data + at));
        const auto entry = static_cast<std::int32_t>(rd32(data + at + 4));
        if (key == 0 && entry == 0)
            break;
        if (entry < 0 || static_cast<std::size_t>(entry) >= s.words())
            throw std::runtime_error("KH2 AI: trigger " + std::to_string(key) + " starts past its end");
        s.triggers.push_back({key, entry});
    }
    if (s.work < 0 || s.frame < 16 || s.stack < 8)
        throw std::runtime_error("KH2 AI: its sizes are not a script's");
    return s;
}

std::vector<std::pair<std::string, Script>> scripts_in(const std::uint8_t *bar, std::size_t size)
{
    std::vector<std::pair<std::string, Script>> out;
    if (size < 16 || std::memcmp(bar, "BAR\x01", 4) != 0)
        return out;
    const std::uint32_t count = rd32(bar + 4);
    for (std::uint32_t i = 0; i < count && 16 + 16 * (std::size_t(i) + 1) <= size; ++i) {
        const std::uint8_t *e = bar + 16 + 16 * std::size_t(i);
        const unsigned kind = e[0] | (e[1] << 8);
        const std::uint32_t at = rd32(e + 8), length = rd32(e + 12);
        if (kind != 3 || length < kHeader || std::size_t(at) + length > size)
            continue;
        std::size_t n = 0;
        while (n < 4 && e[4 + n] != 0)
            ++n;
        out.emplace_back(std::string(reinterpret_cast<const char *>(e + 4), n), read_script(bar + at, length));
    }
    return out;
}

// ------------------------------------------------------------------------------------------------- values

float Value::f() const
{
    return as_float(bits);
}

Value Value::of_float(float v)
{
    return {bits_of(v), Tag::Float};
}

// ------------------------------------------------------------------------------------------------- memory

std::uint32_t Memory::add(std::uint32_t size, std::string what, bool tagged)
{
    const std::uint32_t slots = (size + 3) / 4;
    std::uint32_t base = (next_ + 0xf) & ~0xfu;
    if (std::uint64_t(base) + size + kGap > 0xfffffff0u) {
        // past the top: the first gap big enough from the bottom
        base = 0x00100000;
        for (const auto &[b, r] : regions_) {
            if (std::uint64_t(base) + size + kGap <= b)
                break;
            base = (b + r.size + kGap + 0xf) & ~0xfu;
        }
    }
    Region r;
    r.size = size;
    r.what = std::move(what);
    r.bytes.assign(size, 0);
    if (tagged)
        r.tags.assign(slots, Tag::Unset);
    regions_[base] = std::move(r);
    next_ = base + size + kGap;
    return base;
}

std::uint32_t Memory::add_view(View &view, std::uint32_t size, std::string what)
{
    const std::uint32_t base = add(0, std::move(what));
    auto &r = regions_[base];
    r.size = size;
    r.view = &view;
    next_ = base + size + kGap;
    return base;
}

void Memory::remove(std::uint32_t base)
{
    regions_.erase(base);
}

Memory::Region *Memory::find(std::uint32_t at, std::size_t n, std::uint32_t &offset)
{
    return const_cast<Region *>(static_cast<const Memory *>(this)->find(at, n, offset));
}

const Memory::Region *Memory::find(std::uint32_t at, std::size_t n, std::uint32_t &offset) const
{
    auto it = regions_.upper_bound(at);
    if (it == regions_.begin())
        return nullptr;
    --it;
    if (std::uint64_t(at) + n > std::uint64_t(it->first) + it->second.size)
        return nullptr;
    offset = at - it->first;
    return &it->second;
}

void Memory::miss(std::uint32_t at, const char *how)
{
    // told once by the region it fell nearest (or by its address when none is near)
    std::string near = "nowhere";
    auto it = regions_.upper_bound(at);
    if (it != regions_.begin()) {
        --it;
        near = it->second.what;
    }
    const std::string key = std::string(how) + " " + near;
    if (told_.insert(key).second && report) {
        char line[160];
        std::snprintf(line, sizeof line, "kh2 ai: %s at %#x, outside every region (near %s)", how, at, near.c_str());
        report(line);
    }
}

bool Memory::read(std::uint32_t at, void *out, std::size_t n)
{
    std::uint32_t off = 0;
    Region *r = find(at, n, off);
    if (r == nullptr) {
        std::memset(out, 0, n);
        miss(at, "a read");
        return false;
    }
    if (r->view != nullptr)
        r->view->read(off, out, n);
    else
        std::memcpy(out, r->bytes.data() + off, n);
    return true;
}

bool Memory::write(std::uint32_t at, const void *in, std::size_t n)
{
    std::uint32_t off = 0;
    Region *r = find(at, n, off);
    if (r == nullptr) {
        miss(at, "a write");
        return false;
    }
    if (r->view != nullptr)
        r->view->write(off, in, n);
    else
        std::memcpy(r->bytes.data() + off, in, n);
    return true;
}

std::uint32_t Memory::u32(std::uint32_t at)
{
    std::uint32_t v = 0;
    read(at, &v, 4);
    return v;
}

void Memory::set_u32(std::uint32_t at, std::uint32_t v)
{
    write(at, &v, 4);
}

bool Memory::tagged(std::uint32_t at) const
{
    std::uint32_t off = 0;
    const Region *r = find(at, 4, off);
    return r != nullptr && !r->tags.empty();
}

Tag Memory::tag(std::uint32_t at) const
{
    std::uint32_t off = 0;
    const Region *r = find(at, 4, off);
    if (r == nullptr || r->tags.empty())
        return Tag::Unset;
    return r->tags[off / 4];
}

void Memory::set_tag(std::uint32_t at, Tag tag)
{
    std::uint32_t off = 0;
    Region *r = find(at, 4, off);
    if (r != nullptr && !r->tags.empty())
        r->tags[off / 4] = tag;
}

void Memory::fill_tags(std::uint32_t at, std::size_t slots, Tag tag)
{
    for (std::size_t i = 0; i < slots; ++i)
        set_tag(at + std::uint32_t(4 * i), tag);
}

// ------------------------------------------------------------------------------------------------- calls

void Calls::set_shape(unsigned kind, unsigned number, unsigned args, bool returns)
{
    auto &e = entries_[(kind << 16) | number];
    e.args = args;
    e.returns = returns;
    e.shaped = true;
}

void Calls::set(unsigned kind, unsigned number, Call fn)
{
    entries_[(kind << 16) | number].fn = std::move(fn);
}

const Calls::Entry *Calls::find(unsigned kind, unsigned number) const
{
    const auto it = entries_.find((kind << 16) | number);
    return it == entries_.end() ? nullptr : &it->second;
}

std::vector<std::pair<unsigned, unsigned>> Calls::shaped() const
{
    std::vector<std::pair<unsigned, unsigned>> out;
    for (const auto &[key, e] : entries_)
        if (e.shaped)
            out.emplace_back(key >> 16, key & 0xffff);
    return out;
}

std::vector<std::pair<unsigned, unsigned>> Calls::handled() const
{
    std::vector<std::pair<unsigned, unsigned>> out;
    for (const auto &[key, e] : entries_)
        if (e.fn)
            out.emplace_back(key >> 16, key & 0xffff);
    return out;
}

// ------------------------------------------------------------------------------------------------- the machine

Machine::Machine(std::shared_ptr<const Script> script, Memory &memory, const Calls &calls, void *user)
    : script_(std::move(script)), memory_(memory), calls_(calls), user_(user)
{
    code_ = memory_.add(std::uint32_t(script_->data.size()), script_->name + "'s code");
    memory_.write(code_, script_->data.data(), script_->data.size());
    work_ = memory_.add(std::uint32_t(script_->work), script_->name + "'s work area", true);
}

Machine::~Machine()
{
    if (main_)
        drop_thread(*main_);
    for (auto &t : loose_)
        drop_thread(*t);
    memory_.remove(work_);
    memory_.remove(code_);
}

// FUN_14041b320: the frame's first four slots zero (a return to word 0 ends the thread), the arguments pushed
std::unique_ptr<Thread> Machine::make_thread(std::uint32_t entry, std::span<const Value> args)
{
    auto t = std::make_unique<Thread>();
    t->id = next_id_++;
    t->pc = entry;
    t->frame_base = memory_.add(std::uint32_t(script_->frame) + 16, script_->name + "'s frame", true);
    t->fp = t->frame_base + 16;
    t->stack.reserve(std::size_t(script_->stack) / 8);
    for (const auto &v : args)
        t->stack.push_back(v);
    return t;
}

void Machine::drop_thread(Thread &t)
{
    for (auto &c : t.children)
        drop_thread(*c);
    t.children.clear();
    memory_.remove(t.frame_base);
}

bool Machine::raise(std::int32_t trigger, std::span<const Value> args)
{
    const Trigger *tr = script_->trigger(trigger);
    if (tr == nullptr)
        return false;
    auto t = make_thread(std::uint32_t(tr->entry), args);
    t->status = run(*t);
    for (std::size_t i = 0; i < t->children.size(); ++i)
        tick_tree(*t->children[i]);
    drop_thread(*t);
    return true;
}

bool Machine::run_now(std::uint32_t entry, std::span<const Value> args)
{
    if (entry >= script_->words())
        return false;
    auto t = make_thread(entry, args);
    t->parent = current_;
    t->status = run(*t);
    for (std::size_t i = 0; i < t->children.size(); ++i)
        tick_tree(*t->children[i]);
    drop_thread(*t);
    return true;
}

Thread *Machine::start_main(std::int32_t trigger, std::span<const Value> args)
{
    const Trigger *tr = script_->trigger(trigger);
    if (tr == nullptr)
        return nullptr;
    if (main_)
        drop_thread(*main_);
    main_ = make_thread(std::uint32_t(tr->entry), args);
    return main_.get();
}

Thread *Machine::start_routine(Thread *parent, std::uint32_t entry, std::span<const Value> args)
{
    if (parent == nullptr)
        parent = main_.get();
    if (parent == nullptr || entry >= script_->words())
        return nullptr;
    auto t = make_thread(entry, args);
    t->parent = parent;
    Thread *raw = t.get();
    parent->children.push_back(std::move(t));
    return raw;
}

Thread *Machine::start_loose(std::uint32_t entry, std::span<const Value> args)
{
    if (entry >= script_->words())
        return nullptr;
    loose_.push_back(make_thread(entry, args));
    return loose_.back().get();
}

void Machine::tick_loose()
{
    for (std::size_t i = 0; i < loose_.size(); ++i) // (one started now runs in the same pass)
        tick_tree(*loose_[i]);
    for (auto &t : loose_)
        sweep(*t);
    std::erase_if(loose_, [this](std::unique_ptr<Thread> &t) {
        const bool over = (t->status == Status::Done || t->status == Status::Stopped) && t->children.empty();
        if (over)
            drop_thread(*t);
        return over;
    });
}

void Machine::stop(Thread *thread)
{
    if (thread == nullptr)
        return;
    thread->status = Status::Stopped;
    for (auto &c : thread->children)
        stop(c.get());
}

// FUN_1403e1c80: the thread until it yields or ends, then its children, each once, the new ones too
void Machine::tick_tree(Thread &t)
{
    if (!t.held && (t.status == Status::Running || t.status == Status::Yielded))
        t.status = run(t);
    for (std::size_t i = 0; i < t.children.size(); ++i)
        tick_tree(*t.children[i]);
}

void Machine::sweep(Thread &t)
{
    for (auto &c : t.children)
        sweep(*c);
    std::erase_if(t.children, [this](std::unique_ptr<Thread> &c) {
        const bool over = c->status == Status::Done || c->status == Status::Stopped;
        if (over && c->children.empty()) {
            drop_thread(*c);
            return true;
        }
        return false;
    });
}

void Machine::tick()
{
    if (!main_)
        return;
    tick_tree(*main_);
    sweep(*main_);
}

void Machine::fault(Thread &t, const std::string &why)
{
    t.status = Status::Stopped;
    if (told_.insert(why).second && memory_.report)
        memory_.report("kh2 ai: " + script_->name + ": " + why + "; its thread stopped");
}

// FUN_14041b400, one thread until it yields (Yielded), ends (Done) or cannot go on (Stopped)
Status Machine::run(Thread &t)
{
    const Script &s = *script_;
    const std::size_t capacity = std::size_t(s.stack) / 8;
    const bool sixty = tick_length < 2.0F;
    Thread *const was = current_;
    struct Restore {
        Thread *&slot;
        Thread *value;
        ~Restore() { slot = value; }
    } restore{current_, was};
    current_ = &t;

    const auto push = [&](Value v) {
        if (t.stack.size() >= capacity) {
            fault(t, "its value stack overran");
            return false;
        }
        t.stack.push_back(v);
        return true;
    };
    const auto pop = [&](Value &v) {
        if (t.stack.empty()) {
            fault(t, "it popped an empty stack");
            return false;
        }
        v = t.stack.back();
        t.stack.pop_back();
        return true;
    };
    const auto push_from = [&](std::uint32_t at) { // FUN_14041c780
        Value v{memory_.u32(at), memory_.tagged(at) ? memory_.tag(at) : Tag::Unset};
        return push(v);
    };
    const auto pop_to = [&](std::uint32_t at) { // FUN_14041c6c0
        Value v;
        if (!pop(v))
            return false;
        memory_.set_u32(at, v.bits);
        if (memory_.tagged(at))
            memory_.set_tag(at, v.tag);
        return true;
    };
    // a base and an offset as the operations name them: 0 the frame, 1 the work area, 2 the address in the frame's
    // first word, 3 the script's own code
    const auto based = [&](unsigned base, std::uint16_t offset) -> std::uint32_t {
        switch (base) {
        case 0: return t.fp + offset;
        case 1: return work_ + offset;
        case 2: return memory_.u32(t.fp) + offset;
        case 3: return code_ + std::uint32_t(kCode) + 2u * offset;
        default: return 0;
        }
    };

    for (std::uint64_t steps = 0;; ++steps) {
        if (steps >= budget) {
            fault(t, "it ran " + std::to_string(budget) + " instructions without yielding");
            return Status::Stopped;
        }
        // a thread ended while it runs (a state it is left by a call of its own: FUN_1403caec0 -> FUN_1403e1910 only
        // lists it to be freed) runs on to its yield or its end, as KH2's interpreter does
        const std::uint32_t at = t.pc;
        if (at >= s.words()) {
            fault(t, "its code ran out at word " + std::to_string(at));
            return Status::Stopped;
        }
        const std::uint16_t w = s.word(at);
        const unsigned op = w & 0xf, sub = (w >> 4) & 3, arg = w >> 6;
        const std::uint32_t byte_at = at * 2; // the instruction's byte offset, as the PC's patches name it
        const auto operand = [&](std::uint32_t k) -> std::uint16_t {
            return at + k < s.words() ? s.word(at + k) : 0;
        };
        switch (op) {
        case 0: { // push
            if (sub < 2) {
                const std::uint32_t raw = std::uint32_t(operand(1)) | (std::uint32_t(operand(2)) << 16);
                Value v{raw, sub == 0 ? Tag::Int : Tag::Float};
                if (sub == 1 && sixty) {
                    const float f = as_float(raw);
                    float g = f;
                    // the PC's constants for 60 frames a second, by value and place
                    if (f == 0.83F) {
                        if (byte_at == 0x2e8 || byte_at == 0x3aa4 || byte_at == 0x734 || byte_at == 0x378)
                            g = sixty_curve(f);
                    } else if (f == 0.8F && byte_at == 0x40f2) {
                        g = sixty_curve(f);
                    } else if (f == 2.4F && byte_at == 0x262a) {
                        g = f * 0.5F;
                    } else if (f == 0.08F && byte_at == 0x8410) {
                        g = f * 0.5F;
                    } else if (f == 20.0F && byte_at == 0x180c) {
                        g = 18.0F;
                    } else if (f == 42.0F && byte_at == 0x147a) {
                        if (shared_vector != 0) { // FUN_140431920: the calls' shared vector, its w
                            const float sw = as_float(memory_.u32(shared_vector + 0xc));
                            if (3.141F <= sw && sw < 3.142F)
                                g = 21.0F;
                        }
                    } else if (f == 0.2F && byte_at == 0x6804) {
                        g = f * 0.5F;
                    } else if (f == 0.005F && byte_at == 0x6380) {
                        g = 0.5F * 0.005F;
                    } else if (f == 0.05F && byte_at == 0x109e) {
                        g = 0.5F * 0.05F;
                    } else if (f == 30.0F && byte_at == 0x1a8c && s.name == "b_ex240") {
                        g = f + f;
                    }
                    v.bits = bits_of(g);
                }
                t.pc = at + 3;
                if (!push(v))
                    return Status::Stopped;
            } else if (sub == 2) { // an address
                t.pc = at + 2;
                if (!push(Value::of_address(based(arg, operand(1)))))
                    return Status::Stopped;
            } else { // a value
                t.pc = at + 2;
                const std::uint16_t offset = operand(1);
                if (arg == 3) {
                    const std::uint32_t raw = std::uint32_t(s.word(offset)) | (std::uint32_t(s.word(offset + 1)) << 16);
                    if (!push(Value{raw, Tag::Unset}))
                        return Status::Stopped;
                    break;
                }
                const std::uint32_t where = based(arg, offset);
                // b_ex330: a zero at this place made 0xdeadbeef first
                if (byte_at == 0x2c38 && arg == 2 && offset == 100 && s.name == "b_ex330" && memory_.u32(where) == 0)
                    memory_.set_u32(where, 0xdeadbeefu);
                if (!push_from(where))
                    return Status::Stopped;
            }
            break;
        }
        case 1: { // pop into a base + offset
            if (byte_at == 0x26a && sixty && s.name == "fire" && !t.stack.empty() && t.stack.back().f() == 44.0F)
                t.stack.back().bits = bits_of(44.0F - 2.0F);
            t.pc = at + 2;
            if (arg == 3) { // into a static copy: thrown away
                Value v;
                if (!pop(v))
                    return Status::Stopped;
            } else if (arg > 3) {
                Value v;
                if (!pop(v))
                    return Status::Stopped;
            } else if (!pop_to(based(arg, operand(1)))) {
                return Status::Stopped;
            }
            break;
        }
        case 2: { // copy n bytes from a popped address to a base + offset
            Value src;
            if (!pop(src))
                return Status::Stopped;
            const std::uint16_t n = operand(1), offset = operand(2);
            t.pc = at + 3;
            if (arg >= 3)
                break; // no destination
            const std::uint32_t dst = based(arg, offset);
            if (dst != src.bits) {
                if (memory_.tagged(dst))
                    memory_.fill_tags(dst, n >> 2, Tag::Unset);
                std::vector<std::uint8_t> bytes(n);
                memory_.read(src.bits, bytes.data(), n);
                memory_.write(dst, bytes.data(), n);
            }
            break;
        }
        case 3: { // load from a popped address + offset
            Value a;
            if (!pop(a))
                return Status::Stopped;
            t.pc = at + 2;
            if (!push_from(a.bits + operand(1)))
                return Status::Stopped;
            break;
        }
        case 4: { // store a popped value at a popped address (an argument: copy that many bytes)
            Value v, a;
            if (!pop(v) || !pop(a))
                return Status::Stopped;
            t.pc = at + 1;
            if (arg == 0) {
                memory_.set_u32(a.bits, v.bits);
                if (memory_.tagged(a.bits))
                    memory_.set_tag(a.bits, v.tag);
            } else {
                std::vector<std::uint8_t> bytes(arg);
                memory_.read(v.bits, bytes.data(), arg);
                memory_.write(a.bits, bytes.data(), arg);
                if (memory_.tagged(a.bits))
                    memory_.fill_tags(a.bits, arg >> 2, Tag::Unset);
            }
            break;
        }
        case 5: { // one value
            Value v;
            if (!pop(v))
                return Status::Stopped;
            t.pc = at + 1;
            const std::int32_t i = v.i();
            const float f = v.f();
            Value r = v;
            bool known = true;
            if (sub == 0) {
                switch (arg) {
                case 0: r = Value::of_float(float(i)); break;
                case 2: r = Value::of_int(std::int32_t(0u - v.bits)); break;
                case 3: r = Value::of_int(std::int32_t(~v.bits)); break;
                case 4: case 8: r = Value::of_int(v.bits == 0); break;
                case 5: r = Value::of_int(std::int32_t(i < 0 ? 0u - v.bits : v.bits)); break;
                case 6: r = Value::of_int(std::int32_t(v.bits >> 31)); break;
                case 7: r = Value::of_int(i < 1); break;
                case 9: r = Value::of_int(v.bits != 0); break;
                case 10: r = Value::of_int(std::int32_t((~v.bits) >> 31)); break;
                case 11: r = Value::of_int(i > 0); break;
                default: known = false; break;
                }
            } else if (sub == 1) {
                switch (arg) {
                case 1: r = Value::of_int(std::isfinite(f) && std::fabs(f) < 2147483648.0F ? std::int32_t(f) : INT32_MIN); break;
                case 2: r = Value{v.bits ^ 0x80000000u, Tag::Float}; break;
                case 5: r = Value{v.bits & 0x7fffffffu, Tag::Float}; break;
                case 6: r = Value::of_int(f < 0.0F); break;
                case 7: r = Value::of_int(f <= 0.0F); break;
                case 8: r = Value::of_int(f == 0.0F); break;
                case 9: r = Value::of_int(f != 0.0F); break;
                case 10: r = Value::of_int(f >= 0.0F); break;
                case 11: r = Value::of_int(f > 0.0F); break;
                default: known = false; break;
                }
            } else {
                known = false;
            }
            if (!push(r))
                return Status::Stopped;
            if (!known) { // the value put back, the thread ended with an error (state 5)
                fault(t, "an unknown one-value operation");
                return Status::Stopped;
            }
            break;
        }
        case 6: { // two values
            Value b, a;
            if (!pop(b) || !pop(a))
                return Status::Stopped;
            t.pc = at + 1;
            Value r = a;
            bool known = true;
            if (sub == 1) {
                const float x = a.f(), y = b.f();
                switch (arg) {
                case 0: r = Value::of_float(x + y); break;
                case 1: r = Value::of_float(x - y); break;
                case 2: r = Value::of_float(x * y); break;
                case 3: if (y != 0.0F) r = Value::of_float(x / y); break; // by zero: the left kept
                case 4: r = Value::of_float(std::fmod(x, y)); break;
                default: known = false; break;
                }
            } else if (sub == 0) {
                const std::int32_t x = a.i(), y = b.i();
                switch (arg) {
                case 0: { // an address plus an integer stays an address
                    const bool address = b.tag == Tag::Address || a.tag == Tag::Address ||
                                         (a.tag == Tag::Unset && memory_.tagged(a.bits));
                    r = Value{a.bits + b.bits, address ? Tag::Address : Tag::Int};
                    break;
                }
                case 1: r = Value::of_int(std::int32_t(a.bits - b.bits)); break;
                case 2: r = Value::of_int(std::int32_t(a.bits * b.bits)); break;
                case 3: if (y != 0) r = Value::of_int(std::int32_t(std::int64_t(x) / y)); break; // by zero: the left kept
                case 4: // by zero KH2 itself would stop: told once, the left kept
                    if (y != 0)
                        r = Value::of_int(std::int32_t(std::int64_t(x) % y));
                    else if (told_.insert("a remainder by zero").second && memory_.report)
                        memory_.report("kh2 ai: " + s.name + ": a remainder by zero, the left value kept");
                    break;
                case 5: r = Value::of_int(std::int32_t(a.bits & b.bits)); break;
                case 6: r = Value::of_int(std::int32_t(a.bits | b.bits)); break;
                case 7: r = Value::of_int(std::int32_t(a.bits ^ b.bits)); break;
                case 8: r = Value::of_int(std::int32_t(a.bits << (b.bits & 0x1f))); break;
                case 9: r = Value::of_int(x >> (b.bits & 0x1f)); break;
                case 10: r = Value::of_int(x != 0 && y != 0); break;
                case 11: r = Value::of_int(x != 0 || y != 0); break;
                default: known = false; break;
                }
            } else {
                known = false;
            }
            if (!push(r))
                return Status::Stopped;
            if (!known) {
                fault(t, "an unknown two-value operation");
                return Status::Stopped;
            }
            break;
        }
        case 7: { // jumps, from the instruction after
            const auto by = static_cast<std::int16_t>(operand(1));
            const std::uint32_t next = at + 2, to = std::uint32_t(std::int32_t(next) + by);
            t.pc = next;
            if (arg == 0) {
                t.pc = to;
            } else if (arg == 1 || arg == 2) {
                Value v;
                if (!pop(v))
                    return Status::Stopped;
                if ((v.bits == 0) == (arg == 1))
                    t.pc = to;
            } else {
                fault(t, "an unknown jump");
                return Status::Stopped;
            }
            break;
        }
        case 8:    // a call by a 16-bit distance
        case 11: { // a far call by a 32-bit one
            const std::uint32_t next = at + (op == 8 ? 2 : 3);
            const std::int32_t by = op == 8 ? static_cast<std::int16_t>(operand(1))
                                             : std::int32_t(std::uint32_t(operand(1)) | (std::uint32_t(operand(2)) << 16));
            t.fp += arg * 4;
            if (t.fp > t.frame_base + 16 + std::uint32_t(s.frame)) {
                fault(t, "its frame overran");
                return Status::Stopped;
            }
            memory_.set_u32(t.fp - 4, next);
            memory_.set_u32(t.fp - 8, arg);
            memory_.set_tag(t.fp - 4, Tag::Unset);
            memory_.set_tag(t.fp - 8, Tag::Unset);
            t.pc = std::uint32_t(std::int32_t(next) + by);
            break;
        }
        case 9: { // yield, exit, return, drop, duplicate, the angles
            t.pc = at + 1;
            switch (arg) {
            case 0: return t.status == Status::Stopped ? Status::Stopped : Status::Yielded;
            case 1: return Status::Done;
            case 2: {
                const std::uint32_t back = memory_.u32(t.fp - 4), slots = memory_.u32(t.fp - 8);
                t.pc = back;
                t.fp -= slots * 4;
                if (back == 0)
                    return Status::Done;
                break;
            }
            case 3: {
                Value v;
                if (!pop(v))
                    return Status::Stopped;
                break;
            }
            case 5: {
                if (t.stack.empty()) {
                    fault(t, "it duplicated an empty stack");
                    return Status::Stopped;
                }
                if (!push(t.stack.back()))
                    return Status::Stopped;
                break;
            }
            case 6: case 7: case 8: case 9: {
                Value v;
                if (!pop(v))
                    return Status::Stopped;
                const float x = v.f();
                float r = 0.0F;
                if (arg == 6) {
                    r = std::sin(std::fmod(x + kPi, kTwoPi) - kPi);
                } else if (arg == 7) {
                    const float a = std::fmod(x + kPi, kTwoPi);
                    r = std::fmod(kHalfPi, kTwoPi) - kPi == a - kPi ? 0.0F : std::cos(a - kPi);
                } else if (arg == 8) {
                    r = std::fmod((x * kPi) / kDegrees + kPi, kTwoPi) - kPi;
                } else {
                    r = (x * kDegrees) / kPi;
                }
                if (!push(Value::of_float(r)))
                    return Status::Stopped;
                break;
            }
            default:
                fault(t, "an unknown misc operation");
                return Status::Stopped;
            }
            break;
        }
        case 10: { // a call into the game: table `arg`, entry the next word
            const unsigned number = operand(1);
            t.pc = at + 2;
            const Calls::Entry *e = calls_.find(arg, number);
            const std::string name = std::to_string(arg) + ":" + std::to_string(number);
            if (e == nullptr || !e->shaped) {
                fault(t, "call " + name + " with no known arguments");
                return Status::Stopped;
            }
            if (t.stack.size() < e->args) {
                fault(t, "call " + name + " with too few values on the stack");
                return Status::Stopped;
            }
            std::vector<Value> args(t.stack.end() - std::ptrdiff_t(e->args), t.stack.end());
            t.stack.resize(t.stack.size() - e->args);
            if (args.empty())
                args.push_back(Value{});
            if (e->fn) {
                e->fn(*this, std::span<Value>(args));
            } else {
                if (told_.insert("call " + name).second && memory_.report)
                    memory_.report("kh2 ai: " + s.name + " calls " + name + ", not handled");
                args[0] = Value{0, Tag::Int};
            }
            if (e->returns && !push(args[0]))
                return Status::Stopped;
            break;
        }
        default:
            fault(t, "an unknown operation " + std::to_string(op));
            return Status::Stopped;
        }
    }
}

} // namespace khdays::assets::kh2::ai
