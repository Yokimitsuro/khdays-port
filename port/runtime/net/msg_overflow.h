/* The game's reliable messages past its 32 slots. MsgQueue_Post keeps each message in one of 32 slots until every
 * player has it (MsgQueue_MarkSlotReceived), and with all of them taken it lost the message (0xffff, most callers
 * not looking), and a mod's wave can make any number of enemies at once, each with its parts, regions and summons.
 * Here such a message waits, in order and with the number the game would have given it, and takes a slot as one
 * frees (prepare.py's joins in MsgQueue_Post, MsgQueue_ResendForPeer, MsgQueue_Contains, MsgQueue_Init_2,
 * Session_Shutdown). docs/MULTIPLAYER.md, "The reliable messages". */
#ifndef KHDAYS_MSG_OVERFLOW_H
#define KHDAYS_MSG_OVERFLOW_H

#ifdef __cplusplus
extern "C" {
#endif

/* Whether messages wait for a slot (a new one then waits behind them, so they go in order). */
int khdays_msgqueue_waiting(void);

/* Message `kind` of `size` bytes at `cmd` kept until a slot frees, the queue's (`ctx`: gMsgQueue + 0xc) number
 * taken for it as MsgQueue_Post takes it; that number. */
unsigned short khdays_msgqueue_wait(void *ctx, int kind, const void *cmd, unsigned short size);

/* The waiting messages put in the free slots of `ctx`, the first first, as MsgQueue_Post fills a slot. */
void khdays_msgqueue_drain(void *ctx);

/* Whether the message numbered `handle` still waits (MsgQueue_Contains: not yet had by everyone). */
int khdays_msgqueue_holds(unsigned int handle);

/* The queue made or ended: nothing waits, the frames' buffers as the game made them. */
void khdays_msgqueue_reset(void);

/* A frame's message buffer (MsgQueue_Enqueue): grown past the DS's 0x1e0 bytes, its wireless frame, when the
 * messages of a frame need more -- the game dropped those past it, and with many enemies standing a guest never had
 * the last ones' poses (a second wave's bosses stood in their T pose where they came out). The room of `buffer` (`room` for one never grown), and a buffer grown from `old`. */
int khdays_msgframe_room(const void *buffer, int room);
void khdays_msgframe_grown(const void *old, const void *buffer, int room);

/* A player's slot for the frames received (ChannelBuf_Append): the DS's held 0x3c0 or 0x3d8 bytes, carved from a fixed
 * area, and a frame grown past the DS's was copied beyond it. `old` replaced by `buffer` of `room` bytes for `need`:
 * whether `old` is one grown before (then freed to the game's heap; the first is the fixed area's). */
int khdays_msgslot_grown(const void *old, const void *buffer, unsigned int room, unsigned int need);

#ifdef __cplusplus
}
#endif

#endif
