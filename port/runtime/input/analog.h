/* Analog movement's arithmetic (analog.c): pure functions, testable on their own. The game moves the
 * player toward a heading made from the D-pad (Ov022_HeadingFromHeldDpad): Up 0, Left 0x3fff, Down
 * 0x8000, Right 0xbfff, counterclockwise, 0x10000 a turn, relative to the camera. The stick gives that
 * heading at any angle, and a walk below the run threshold. */
#ifndef KHDAYS_ANALOG_H
#define KHDAYS_ANALOG_H

/* The heading the stick points to (x, y in -32767..32767, y up): exactly the game's value on a D-pad
 * direction; -1 at rest. */
int khdays_analog_heading(int x, int y);

/* The stick's heading turned as the game turned the D-pad's (`dpad`): Flip Foot swaps Up/Down and
 * Left/Right (Ov022_SwapPairedStateFlags), so the stick must turn around too. */
int khdays_analog_heading_turned(int stick, int dpad);

/* The stick as KH2 reads it (YS::HARDPAD::get_stick_value, 0x473da8): a dead zone off each axis alone, the rest
 * rescaled to full travel. An axis under it is exactly 0; both under it is 0 0, no direction at all. For the KH2
 * camera's chase: the player's heading keeps the stick's own angle. */
void khdays_analog_kh2_stick(int *x, int *y);

/* How far the stick is pushed, in percent of its travel (0..100). */
int khdays_analog_tilt(int x, int y);

/* The speed to move toward: the game's own `rate` when the stick is pushed
 * at least `run_threshold` percent, `walk_speed` percent of it below that.
 * Never faster than the game's own. */
int khdays_analog_rate(int rate, int tilt, int run_threshold, int walk_speed);

#endif
