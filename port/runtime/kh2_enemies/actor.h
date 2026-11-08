// A KINGDOM HEARTS II enemy as its script sees it (docs/KH2_ENEMIES.md): KH2's actor laid out at KH2's offsets (the
// calls read and change it there, and a script may read it directly), its state machine, and the script machine that
// drives it. Nothing here touches Days's game: the shell (shell.cpp) links an actor to a Days enemy.
#pragma once

#include "khdays/assets/kh2_ai.h"

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <vector>

namespace khdays::kh2_enemies {

namespace ai = khdays::assets::kh2::ai;

// KH2's actor's offsets the port reads and writes (each named where a call or the game uses it)
namespace field {
constexpr std::uint32_t kFlags = 0x120;     // bit words: 1:65, 1:70, 1:71
constexpr std::uint32_t kStatus = 0x18c;    // bits read by 1:14, 1:78, 2:5, 1:60
constexpr std::uint32_t kPlace = 0x670;     // vec4: 1:147
constexpr std::uint32_t kFacing = 0x680;    // vec4: 1:79, 1:201
constexpr std::uint32_t kYaw = 0x7a8;       // float: 1:4, 1:79
constexpr std::uint32_t kAttacker = 0xd38;  // the last to hit it (FUN_140410700), 2:88
constexpr std::uint32_t kSize = 0xe00;      // past every offset read so far
} // namespace field

// One state of the table a script fills (1:5, 1:6; FUN_1403ca0e0): 32 bytes in the script's own memory.
struct StateEntry final {
    std::uint32_t at = 0;        // its address
    std::string name;            // its id: a name in the script's code
    std::uint16_t priority = 0;  // +4
    std::uint16_t flags = 0;     // +6: bit 1 locks it while it is the state (FUN_1403cad00)
    std::uint16_t routine = 0;   // +8: run as a child thread while it is the state
    std::uint16_t on_leave = 0;  // +0xa: run at once when it is left (FUN_1403caec0)
    std::uint16_t on_enter = 0;  // +0x16: run at once after it is entered (FUN_1403cad00)
};

class Actor;

// The state machine at KH2's actor + 0x390 (FUN_1403cae60 enter, FUN_1403cab00 queue, FUN_1403cad00, FUN_1403caec0,
// FUN_1403ca2c0 the queue's next). States are found by name (FUN_1403ca700), so the game asks for them by name too
// ("dead", "leave", a reaction).
class StateMachine final {
public:
    void set_table(std::uint32_t table, std::uint32_t handle) { table_ = table, handle_ = handle; }
    bool enter(Actor &a, const std::string &name);  // 1:8: at once, the queue emptied (not while locked)
    bool queue(Actor &a, const std::string &name);  // 1:9: after the current one (16 at most)
    bool pending() const;                           // 1:10: one running or queued
    void clear(Actor &a);                           // 1:127: the current left, the queue emptied
    void tick(Actor &a);                            // the current one ended: the next queued entered
    const StateEntry *current() const { return current_ ? &*current_ : nullptr; }
    std::function<void(const std::string &)> entered; // told each state entered (the research's runs, the logs)
    bool find(Actor &a, const std::string &name, StateEntry &out) const;

private:
    void leave(Actor &a);
    void go(Actor &a, const StateEntry &e);

    std::uint32_t table_ = 0, handle_ = 0;
    std::vector<std::string> queued_;
    std::optional<StateEntry> current_;
    ai::Thread *thread_ = nullptr;
    std::uint32_t thread_id_ = 0;
    bool locked_ = false;
};

// Every access a script makes to an actor's memory directly, kept to learn which offsets it reads (the research's
// watched run) -- off unless asked.
struct Watch final {
    bool on = false;
    std::map<std::uint32_t, unsigned> reads, writes; // offset -> how many
};

class Actor final : public ai::View {
public:
    Actor(std::shared_ptr<const ai::Script> script, ai::Memory &memory, const ai::Calls &calls, std::string kind);
    ~Actor() override;

    // ai::View: the actor's bytes at KH2's offsets
    void read(std::uint32_t offset, void *out, std::size_t n) override;
    void write(std::uint32_t offset, const void *in, std::size_t n) override;

    std::uint32_t address() const { return address_; } // where the script sees it
    ai::Machine &machine() { return *machine_; }
    StateMachine &states() { return states_; }
    const std::string &kind() const { return kind_; }
    Watch watch;

    float f(std::uint32_t offset) const;
    std::uint32_t u32(std::uint32_t offset) const;
    void set_f(std::uint32_t offset, float v);
    void set_u32(std::uint32_t offset, std::uint32_t v);

    // Its start (trigger 0 with its address, then trigger 1 as its main thread; FUN_1403b5f30), a tick (its state
    // machine, then its threads) and the game's events.
    void start();
    void tick();
    bool event(std::int32_t trigger, std::span<const ai::Value> args = {});

    // The actor a script address names (an AI handle's +4, or an actor's own address), or null.
    static Actor *at(ai::Memory &memory, std::uint32_t address);
    static Actor *of_handle(ai::Machine &m, std::uint32_t handle);

private:
    std::string kind_;
    ai::Memory &memory_;
    std::vector<std::uint8_t> bytes_;
    std::uint32_t address_ = 0;
    std::unique_ptr<ai::Machine> machine_;
    StateMachine states_;
};

// The name a script address holds (a state's id): up to its NUL, 64 characters at most.
std::string name_at(ai::Memory &memory, std::uint32_t address);

} // namespace khdays::kh2_enemies
