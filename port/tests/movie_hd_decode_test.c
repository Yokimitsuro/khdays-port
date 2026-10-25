/* The HD video decoder (movie_hd_win.c) on one of the player's files, played to its end at its own pace
 * with its sound, every picture taken as a 60 Hz display would: it must end within a second of the
 * file's length, or of the part asked for (third and fourth arguments, from and until in seconds:
 * khdays_hd_open_part), no picture outside it, coming up from black over its first half second and going
 * down to it over its last (khdays_hd_fades). Without the file the test is skipped (the videos are the
 * player's, never the repo's). */
#include "../host/movies/movie_hd.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>

/* the Menu Overhaul (runtime/mods/installed.h) not installed: the theater's music as the port's own */
int khdays_mod_installed(int mod)
{
    (void)mod;
    return 0;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "data/cutscenes/cinematics/hd808.mp4";
    const double file_length = argc > 2 ? atof(argv[2]) : 7.2;
    const double from = argc > 3 ? atof(argv[3]) : 0.0, end = argc > 4 ? atof(argv[4]) : 0.0;
    const double length = (end > 0.0 ? end : file_length) - from;
    KhdaysHdInfo info;
    char why[320];
    FILE *f = fopen(path, "rb");
    Uint64 start, last_change;
    long pictures = 0;
    double first = -1.0, latest = 0.0;                         /* the first and the last picture's times */
    float first_level = -1.0f, last_level = 1.0f, most = 0.0f; /* their levels through the fades; the most */
    int status = 0;
    if (f == NULL) {
        printf("movie_hd_decode_test: no %s; skipped\n", path);
        return 0;
    }
    fclose(f);
    if (!SDL_Init(SDL_INIT_AUDIO)) {
        printf("movie_hd_decode_test: no audio (%s)\n", SDL_GetError());
        return 1;
    }
    khdays_hd_fades(0.5, 0.5);
    if (!khdays_hd_open_part(path, from, end, &info, why, sizeof(why))) {
        printf("movie_hd_decode_test: %s cannot be played: %s\n", path, why);
        return 1;
    }
    printf("movie_hd_decode_test: %s: %dx%d, %.3f frames a second, %d channels at %d Hz\n", path, info.width,
           info.height, info.fps, info.channels, info.rate);
    khdays_hd_set_gain(0.0f); /* silent: a test */
    khdays_hd_start();
    start = last_change = SDL_GetTicks();
    while (SDL_GetTicks() - start < (Uint64)((length + 5.0) * 1000.0)) {
        KhdaysHdPicture p;
        if (khdays_hd_picture_begin(&p)) {
            ++pictures;
            first = first < 0.0 ? p.time : first;
            first_level = first_level < 0.0f ? p.level : first_level;
            latest = p.time;
            last_level = p.level;
            most = SDL_max(most, p.level);
            khdays_hd_picture_done();
        }
        status = khdays_hd_status(why, sizeof(why));
        if (status != 0) {
            break;
        }
        SDL_Delay(16);
    }
    {
        const double took = (double)(SDL_GetTicks() - start) / 1000.0;
        printf("movie_hd_decode_test: status %d after %.2f s, %ld pictures taken, from %.3f s to %.3f s%s%s\n", status,
               took, pictures, first, latest, status == -1 ? ": " : "", status == -1 ? why : "");
        printf("movie_hd_decode_test: the fades: the first picture at %.2f, the last at %.2f, the most %.2f\n",
               first_level, last_level, most);
        khdays_hd_close();
        SDL_Quit();
        return status == 1 && took < length + 1.0 && took > length - 1.5 && first > from - 0.05 &&
                       (end == 0.0 || latest < end) && first_level < 0.2f && last_level < 0.2f && most == 1.0f
                   ? 0
                   : 1;
    }
}
