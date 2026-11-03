/* Analog movement's arithmetic (analog.h). */
#include "analog.h"

#include <math.h>

#define PI 3.14159265358979323846
#define FULL_TURN 0x10000
#define FULL_STICK 32767
#define KH2_STICK_SENSE (FULL_STICK * 8 / 17) /* YS::StickSense, 60 of 127.5 (YS::PAD::Init) */

int khdays_analog_heading(int x, int y)
{
    double turns;
    int heading;
    if (x == 0 && y == 0) {
        return -1;
    }
    /* The game's Left and Right are one unit short of a quarter turn (its sum starts from -1); a stick
     * held exactly left or right gives the same. */
    if (y == 0) {
        return x < 0 ? 0x3fff : 0xbfff;
    }
    /* Counterclockwise from Up: straight left (-x) is a quarter turn. */
    turns = atan2(-(double)x, (double)y) / (2.0 * PI);
    heading = (int)floor(turns * FULL_TURN + 0.5);
    return heading & (FULL_TURN - 1);
}

int khdays_analog_heading_turned(int stick, int dpad)
{
    /* the D-pad's bits come from the same stick: within an eighth of a turn of it unless turned */
    const int apart = (dpad - stick) & (FULL_TURN - 1);
    if (apart >= FULL_TURN / 4 && apart <= FULL_TURN * 3 / 4) {
        return (stick + FULL_TURN / 2) & (FULL_TURN - 1);
    }
    return stick;
}

static int kh2_axis(int value)
{
    const int pushed = value < 0 ? -value : value;
    const int reach = pushed > FULL_STICK ? FULL_STICK : pushed;
    int out;
    if (reach <= KH2_STICK_SENSE) {
        return 0;
    }
    out = (int)((long long)(reach - KH2_STICK_SENSE) * FULL_STICK / (FULL_STICK - KH2_STICK_SENSE));
    return value < 0 ? -out : out;
}

void khdays_analog_kh2_stick(int *x, int *y)
{
    *x = kh2_axis(*x);
    *y = kh2_axis(*y);
}

int khdays_analog_tilt(int x, int y)
{
    const double pushed = sqrt((double)x * x + (double)y * y) * 100.0 / 32767.0;
    /* A square gate's corners pass full travel. */
    return pushed >= 100.0 ? 100 : (int)pushed;
}

int khdays_analog_rate(int rate, int tilt, int run_threshold, int walk_speed)
{
    int walk;
    if (tilt >= run_threshold) {
        return rate;
    }
    if (walk_speed < 0) {
        walk_speed = 0;
    } else if (walk_speed > 100) {
        walk_speed = 100;
    }
    walk = rate * walk_speed / 100;
    return walk < rate ? walk : rate;
}
