/* The session log: what a session did, for the player to send when something seems wrong (scenes,
 * file reads, music starts and their delays, stretches without a frame, F12's marks).
 * saves/logs/session-YYYYMMDD-HHMMSS.log, one line per event: "<seconds> f<frame> <what>". On in
 * Debug unless KHDAYS_LOG=0, in Release with KHDAYS_LOG=1 or the settings' Session log. Flushed once
 * a second, on a mark, at exit and on a crash. Callable from any thread. */
#ifndef KHDAYS_SESSION_LOG_H
#define KHDAYS_SESSION_LOG_H

#include "../../hal/hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Opens the log if it is on (once; at start): in the Debug build unless
 * KHDAYS_LOG=0, in Release with KHDAYS_LOG=1 -- or when the player's
 * settings ask for it (khdays_log_want). */
void khdays_log_open(void);

/* The settings' "Session log" (the host, when they are read and when they
 * change): on opens a log now if none is open; off closes one it opened. */
void khdays_log_want(int on);

/* Whether the log is on and open. */
int khdays_log_on(void);

/* The frame the lines carry (the host's, once a frame); flushes once a
 * second. */
void khdays_log_frame(u32 frame);

/* One line (no newline in `format`). */
void khdays_log(const char *format, ...);

/* F12: a line the player can find. */
void khdays_log_mark(void);

/* How many marks F12 has made (0: none, or no log). */
u32 khdays_log_marks(void);

/* Flushes and closes (exit, crash). */
void khdays_log_close(void);

#ifdef __cplusplus
}
#endif

#endif
