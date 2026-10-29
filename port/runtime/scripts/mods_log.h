/* The mods' log, logs/mods.log in the player's saves folder: what each mod printed, its errors and its
 * state changes, a line each, for the player and khdays-studio's console:
 *   <seconds since the start> <mod id> <kind> <file>:<line> <text>
 * Each line also goes to the session log, after "mods: ". */
#ifndef KHDAYS_SCRIPTS_MODS_LOG_H
#define KHDAYS_SCRIPTS_MODS_LOG_H

/* Starts the log afresh in `folder`/logs (the last run's kept as mods.prev.log); opened again in the same
 * run, it goes on. */
void khdays_mods_log_open(const char *folder);

/* One entry; `file` NULL (or `line` <= 0) when none applies. A text of several lines gives a line
 * each, of the same kind. */
void khdays_mods_log(const char *id, const char *kind, const char *file, int line, const char *text);

void khdays_mods_log_close(void);

#endif
