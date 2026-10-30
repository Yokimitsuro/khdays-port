/* The window's picture (gpu_present.c): the frames the game's thread hands over
 * (runtime/display/frame_package.h), drawn on the graphics card through an SDL GPU device, the window's
 * SDL_Renderer made on it so the settings menu draws as it always has; or through SDL's own renderer
 * when there is no card to use. Window's thread only. */
#ifndef KHDAYS_GPU_PRESENT_H
#define KHDAYS_GPU_PRESENT_H

#include "../app/profile.h"

#include <SDL3/SDL.h>
#include <stdint.h>

typedef struct KhdaysFramePackage KhdaysFramePackage; /* runtime/display/frame_package.h */

/* Makes the window's renderer: on a GPU device when one can be made (its backend logged), else SDL's
 * own; `notice` hears once why the card is not used. NULL when neither can be made. */
SDL_Renderer *khdays_present_open(SDL_Window *window, KhdaysNotice notice);

/* Lets go of the renderer and the device (the window stays). */
void khdays_present_close(void);

/* Whether the picture goes through the GPU device (the Video settings that
 * need the graphics card can be used). */
int khdays_present_gpu(void);

/* The graphics card stopped answering: nothing is drawn on it until the window makes the renderer
 * again, as for SDL_EVENT_RENDER_DEVICE_LOST (khdays_present_event). */
int khdays_present_lost(void);

/* What the window needs in the frame packages for the picture settings (KHDAYS_WANT_*): 0 at internal
 * resolution 1. */
int khdays_present_wants(void);

/* How wide the 3D lists should be, in DS pixels (256: the DS's; more in
 * widescreen): for khdays_host_frame_widen. */
int khdays_present_columns(void);

/* The internal resolution the port's 2D is composed at (1 without it). */
int khdays_present_scale(void);

/* The window changed size: the "window" resolution and aspect follow it. */
void khdays_present_resized(void);

/* The newest frame: from a package, or (khdays_host_present) the upper and
 * the lower screen one after the other, 256x192 each. */
void khdays_present_package(const KhdaysFramePackage *package);

/* The 3D at the monitor's rate: whether the newest package is drawn again at each refresh
 * (picture_pace smooth), the 3D placed between the two latest logic frames at the refresh's time (at
 * most one logic frame behind). Returns 0 when there is nothing new to draw. */
int khdays_present_smooth(void);
int khdays_present_refresh(void);

/* An animated backdrop (the title's) moves at the display's pace even when a still menu sends no
 * frames: whether the latest picture has one, and drawing the newest package again for it (not while the
 * game's own frames keep coming: each draws it, its motion with it). */
int khdays_present_backdrop_moving(void);
int khdays_present_refresh_backdrop(void);

/* The one-screen situation still settling while the game sends no frames (onescreen.h): whether the
 * newest package is to be decided and drawn again, and that drawing, once a VBlank has passed. */
int khdays_present_settling(void);
int khdays_present_refresh_settling(void);

/* Whether the latest one-screen picture has a settings button of its own (an animated backdrop's
 * `button open_settings`), and whether a window point (SDL's mouse coordinates) is on one. */
int khdays_present_settings_button(void);
int khdays_present_settings_button_at(float x, float y);

/* Video: Show FPS. Whether it is on, and the pictures shown and the game's
 * logic frames a second, measured over the last half second. */
int khdays_present_fps(int *pictures, int *frames);
void khdays_present_screens(const uint32_t *upper_then_lower);

/* The window's drawing of a picture (redraw): how long it took, and how long presenting it waited (for
 * the graphics card and the display) -- the session log's timings while the Moogle Shop shows. */
void khdays_present_redraw_took(uint64_t drawing_ns, uint64_t presenting_ns);

/* The picture settings, from the next frame drawn. */
void khdays_present_set_video(const KhdaysVideo *video);

/* Draws the picture; the caller draws the settings menu over it and
 * presents. */
void khdays_present_draw(void);
/* Whether the window shows the one-screen picture of the last frame made. */
int khdays_present_one_picture(void);

/* A render event: after a device reset the picture is uploaded again; after a lost device the
 * renderer is made again. Returns the renderer to use from now on (`*renewed` set when new: the
 * caller's textures for the old one are gone). */
SDL_Renderer *khdays_present_event(const SDL_Event *event, int *renewed);

#endif
