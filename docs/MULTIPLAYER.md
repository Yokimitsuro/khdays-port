# How the port plays Mission Mode's multiplayer

The game was made for the DS's wireless. The port's wireless layer
(`port/hal/sdk_wireless.c`) stands in for NitroSDK's WM library and carries the
game's messages over UDP (`port/runtime/net/net_session.c`); what the game does
with them -- the host simulates the battle, the guests send their controls -- is
in `RUNTIME_MAP.md` section 16. Running a server: `port/server/README.md`.

## The session

- Rooms are announced on the local network (copies on the same PC see them
  too) and, online, through `khdays-server`, which can also relay datagrams
  between two players.
- One copy hosts and the others join it: the members form a star around the
  host.
- The game's messages travel as numbered records, each resent until
  acknowledged and handed to the game in order, whatever the network loses or
  reorders.
- A thread of its own receives and resends, keeping the session alive while the
  game is busy or paused; the game's thread sends and takes what arrived
  (`khdays_net_next`). Every list grows as needed. The session takes plain
  values and its events hold no pointer, so the game's side and the port's read
  them the same way.

A control packet (gate 3, `Ov022_SendControlPacket`) holds a halfword, then the
slot and the buttons held, the command chosen (its kind in the low 4 bits of
the second word; 7 means none) and the target. It has only the D-pad, so a guest
also sends its movement stick (a STICK datagram, not acknowledged: x and y,
its run threshold and walk speed, and whether analog movement is on) with its
controls, when the stick changed or a fifth of a second after the last; the
host moves that guest's actor with it (its exact angle, and its walk when the
stick is pushed less than the run threshold), as it does its own.

### The mods' records

The scripted mods (`docs/SCRIPTING.md`) have records of their own on port 15,
which the game never opens (it opens 12, and ov105's data port 14): the session
hands them to the scripts and never to the game. Each starts with a kind byte:

- **the room's seed** (1, then a u64): chosen as the room opens, sent to each
  member as it joins and to all of them whenever the host changes it (as each
  mission ends), so the next mission starts with the same seed on every copy;
  the mods' `random` draws from it, every mod its own run, started again at
  each mission's start.
- **a shared message** (2, then a byte 1 on its last piece, then its bytes):
  what a host's mod shares (`mod.share`), in pieces of up to 1 KB, put together
  again on the guest; the pieces come in order as every port's records do, so a
  message of any size arrives whole. It starts with the mod's id, then the table
  (nil, booleans, numbers, texts and tables of them), heard as `shared` by the
  guest's copy of that mod.

### The playout store

The network delivers a guest's controls unevenly (two in one tick, none in the
next); handed as they came, the host would hold a button a tick longer or
shorter than the guest did. So in a battle the host keeps a few of each
member's controls in store and hands them to the game one per tick:

- the store fills up to its depth before handing out the first control;
- each tick that passes with no control handed (it "ran dry"), the depth grows
  by one, up to the longest recent gap;
- if it stayed too full for a whole second, one repeated control (never a
  command) is dropped;
- after ten seconds without running dry, the depth shrinks by one (and every
  ten seconds to the longest gap if it is deeper).

### The reliable messages

The game keeps each reliable message (`MsgQueue_Post`: a region's join or leave,
an enemy's status, a hit, the field's commands) in one of 32 slots, sent again
every frame until every player has it (`MsgQueue_ResendForPeer`,
`MsgQueue_MarkSlotReceived`). With all 32 taken it lost the message: it answered
0xffff, and most callers -- the enemies' -- do not look. The walk below never
filled them, but a mod's wave can bring any number of enemies at once, each with
its parts, regions and summons, so the port lets such a message wait (`runtime/net/msg_overflow.c`, joined in `MsgQueue_Post`, `ResendForPeer`,
`MsgQueue_Contains`, `MsgQueue_Init_2` and `Session_Shutdown`): numbered as the
game would number it, in order -- a message posted while others wait waits
behind them --, put in a slot as one frees, the first first; `MsgQueue_Contains`
says a waiting one is not had by everyone yet. The session log says when they
first wait and the most that waited.

Each frame's messages, reliable or not, go out in one buffer (`MsgQueue_Enqueue`)
of 0x1e0 bytes, the DS's wireless frame; a message past it was dropped. The
enemies' poses (`Ov107_SendPoseMessage`: a byte of state, the position and the
turn, about 32 bytes each) fill it with some fifteen enemies standing, and the
ones listed after them never reached a guest, frame after frame. Seen
(2026-11-04, the walk `multiplayer_boss_waves`: two each of Darkside, Dual Blade
and Guard Armor in its second wave): on the guest the first Dual Blade, its parts
and the second Darkside's summons -- ids 21 to 41 -- stood at 0,0,0 with full HP,
their state byte never set, unmoving in their T pose, while the host's fought.
The port grows the buffer when a frame's
messages need more (to 0xffe0 bytes: the game's sizes are 16 bits) and the
wireless layer's receiving one holds any record (`hal/sdk_wireless.c`); the
session log says how large a frame grew. The receiving end had the DS's sizes
too: each player's slot for the frames received (`Session_LayoutPacketSlots`:
a guest's two of 0x3c0 bytes, a host's two of 0x3d8 and three of 0xc0, carved
from a fixed area) took what `ChannelBuf_Append` copied without a look at its
size, and the first frames past the DS's (1,982 bytes in the walk's second wave,
the host's buffer grown to 3,840) wrote past the guest's slot and stopped it. A slot now grows in the game's heap when what
it holds needs more (the fixed area's first, a grown one freed when it grows
again), and each message's copy for its handler holds a message's 10-bit size
(0x400 bytes, was 0x3c0).

The guest finds each message's actor by its id (`Ov107_FindMessageHandler`) in
the table the area made of its actors (`Ov107_Scene_BuildIdTable`). The table
holds the ids there were then, and the game read it for any id under the count
now: past its end for an actor made after it (a mod's enemy, a later area's).
The port reads it for the ids it was made for and finds the others in the
actors' list, as the game does without a table (`khdays_enemies_ids_room`,
joined in both).

## A guest's own character, predicted

Off by default (`KHDAYS_NET_PREDICT=1` turns it on: online the host corrected it
often, once by 46 units after an area change, and the character fell through the
floor). The battle runs on the host, so a guest's own character reacts a round
trip late; with the prediction the guest runs it itself, as a host does
(`runtime/net/mp_predict.c`), through `prepare.py`'s hooks:

- `ROLE_QUERY`: the game's "am I the host?" checks answer host while the own
  character updates (not `ROLE_KEPT`'s, which touch the others or the world;
  `BUILD.md`, "Multiplayer");
- `Ov022_Party_SyncAndUpdate`: the own character's update is the window
  (`khdays_mp_predict_open`);
- `Ov022_Party_ApplyNetState`: the host's record is compared with the predicted
  path (`khdays_mp_predict_keep`) and the character moved by the difference, or
  the host's version shown during a hit;
- `MsgQueue_SendGate`: what the game sends in the window is left to the host,
  but for its shots, applied at once (the host's copies are filtered out when
  they come).

What it reads:

- A control packet is 12 bytes; its second halfword holds the slot (bits 0-2)
  and the buttons held (bits 3-14). The input table (`Ov022_PublishRequestToSlot`)
  has a "taken" flag at +0x10 + slot, the packet at +0x14 + slot x 12, and the
  buttons held, pressed and held before at +0x84, +0x86 and +0x88 + slot x 6.
- The predicted path is the own character's positions since the host's record
  last matched one: x, y, z (fx32), the animation (+0x6bc), the tick and the
  heading (the node's angle at +0x80 minus 0x8000, as the host's record has it).
  The host runs the same buttons a round trip later, so its record is compared
  with the closest step that shows the same animation.
- The own character's shots (gate 9, `Ov022_RunShotCommand`: operation, owner
  and mode in the first byte, the channel in the second, the shot or its
  descriptor in the third and fourth) are kept, with their tick, until the
  host's copy comes or the host's records pass that tick without it. A shot the
  host never made gives back the unit of budget it spent: only the host's
  commands refill it, so the guest must spend exactly what the host does. The
  actor's channels are at +0x2644, 0x30 bytes each, the budget at +0x18 and its
  maximum at +0x19; its descriptors at +0x10, 0x50 bytes each, whose flag 1
  makes a shot free. A command's channel is in the low 3 bits of its second
  byte, its descriptor in bits 3-5 of byte 0x16.

`RUNTIME_MAP.md`, "A guest's own character, predicted", has the measurements.

## The list of rooms

The port draws Mission Mode's "Select a group to join" (`runtime/net/roomlist.c`)
with every room there is -- its name, its players, the ping a session with it
would take, whether it asks for a password -- a search by name, and a name and
password for a room created. With the HD collection's pieces it draws its own
screens (`host/screens/room_screen.c`, `layouts/rooms/<lang>/rooms.txt`);
without them the game's own screens draw the same list (`ui/rooms2d.c`,
`ui/room_icons.cpp`).

The game's own list runs underneath (ov006's in the character select, scene 7,
and the Mission Mode camp's in ov008, scene 19), and keeps taking the buttons:
the port tells it only of the room under its cursor (`khdays_net_scan_only`),
and to join or create one it takes the game's cursor there and hands it A. The
game's joining, its members' screen and its errors are its own
(`RUNTIME_MAP.md` section 16.2).

The members' screen shows each player's level, which the game does not send:
the port's pings carry it. A guest's ping gives its own level after the time it
was sent (u16); the host's gives every level it knows (a count u8, then an aid
u8 and a level u16 for each, its own too), so a guest that joins late learns the
others' within a ping. A copy's level is the game's record of its player once
the game made it (the Mission Mode camp makes it from the panels), else the
level the title's list counted for the file loaded (`savelist.c`, the game's own
count of the file's panels; 1 for the guest game, which starts anew). On the HD
screens it is in the card's upper right corner, on the game's own screens before
the player's detail.

## khdays-server

Where players meet over the internet (`port/server`). Hosts register their
rooms and guests ask for the list; when a guest picks a room, the server tells
each side the other's public address so they can talk directly, and relays
their datagrams if their networks do not allow it. `server_core.c` is the
protocol, kept apart from the program around it (`server.c`: options, the
window's messages, the router, the loop) so the fuzzer tests the same code. The
same code runs on Windows next to the game and as a service on Linux
(`host/net/net_win.c`, `net_posix.c`).

It keeps three kinds of records: groups (rooms hosts registered), pairs (a guest
introduced to a host, kept for relaying) and indexes (for each game and build,
its groups, which LIST pages through). Records live in slabs -- growing arrays
whose slots never move, with a list of free slots -- found by hash tables, and
groups and pairs are chained from the least to the most recently heard from:
when the memory budget is full the oldest is evicted, and records silent too
long are forgotten. What it keeps never passes the budget, each datagram takes
the same time however much it keeps, and it logs nothing per datagram, only
counts: no player's address is ever written, in the window or the log.

## Fuzzing

`port/fuzz` holds libFuzzer targets (`port/tools/fuzz.py`; the port's ctest runs
each for 15 seconds):

- `fuzz_server.c`: each input becomes datagrams from a few addresses (or a
  forged one) after some time, with a valid header or raw bytes, fed to the
  server's core with a small memory budget on a fake network. Besides
  AddressSanitizer's checks, the run stops if an answer is ever longer than the
  datagram that asked, or the memory in use passes the budget.
- `fuzz_session.c`: `net_session.c` compiled in, so its static reader is
  reached, on the stand-in network (`net_stub.h`), as a host, a guest joining on
  the local network or through the server, or a player looking for groups; the
  input becomes datagrams from a few addresses (the server's among them),
  between which the session's timer and its delivery to the game run.
- `fuzz_portmap.c`: a router's answers to the port mapping requests.

An input that breaks a reader is kept in `port/fuzz/<target>/` and replayed by
the tests from then on.
