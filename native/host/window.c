/* The window: SDL3 on its own thread. The game side hands over finished
 * frames (khdays_host_present) and reads the input state; both cross threads
 * through a lock and interlocked words. Keys follow the port's defaults
 * (platform/pc/overlay_ui.h): arrows, Z = A, X = B, S = X, A = Y, Q = L,
 * E = R, Enter = Start, right Shift = Select; the mouse on the lower screen
 * is the stylus. Closing the window ends the process. The sound goes to an
 * SDL audio stream, which converts the DS's rate to the device's. */
#include "host.h"
#include "../runtime/input.h"
#include "../runtime/sound.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

#define W 256
#define H 192

static SRWLOCK frame_lock = SRWLOCK_INIT;
static uint32_t frame[2 * W * H];
static volatile LONG frame_pending;
static Uint32 frame_event;

static volatile LONG buttons;
static volatile LONG touch;  /* bit 31: touching; x in bits 0-7, y in 8-15 */

static HANDLE ready;
static volatile LONG started;

static SDL_AudioStream *volatile audio;

static const struct {
    SDL_Scancode code;
    uint16_t key;
} bindings[] = {
    {SDL_SCANCODE_UP, KHDAYS_KEY_UP},       {SDL_SCANCODE_DOWN, KHDAYS_KEY_DOWN},
    {SDL_SCANCODE_LEFT, KHDAYS_KEY_LEFT},   {SDL_SCANCODE_RIGHT, KHDAYS_KEY_RIGHT},
    {SDL_SCANCODE_Z, KHDAYS_KEY_A},         {SDL_SCANCODE_X, KHDAYS_KEY_B},
    {SDL_SCANCODE_S, KHDAYS_KEY_X},         {SDL_SCANCODE_A, KHDAYS_KEY_Y},
    {SDL_SCANCODE_Q, KHDAYS_KEY_L},         {SDL_SCANCODE_E, KHDAYS_KEY_R},
    {SDL_SCANCODE_RETURN, KHDAYS_KEY_START}, {SDL_SCANCODE_RSHIFT, KHDAYS_KEY_SELECT},
};

static void read_keys(void)
{
    const bool *state = SDL_GetKeyboardState(NULL);
    LONG held = 0;
    for (size_t n = 0; n < sizeof(bindings) / sizeof(bindings[0]); ++n) {
        if (state[bindings[n].code]) {
            held |= bindings[n].key;
        }
    }
    InterlockedExchange(&buttons, held);
}

static void read_mouse(SDL_Renderer *renderer, float wx, float wy, int down)
{
    float x, y;
    if (!down) {
        InterlockedExchange(&touch, 0);
        return;
    }
    SDL_RenderCoordinatesFromWindow(renderer, wx, wy, &x, &y);
    if (x >= 0 && x < W && y >= H && y < 2 * H) {
        InterlockedExchange(&touch, (LONG)(0x80000000u | (unsigned)x | (unsigned)(y - H) << 8));
    } else {
        InterlockedExchange(&touch, 0);
    }
}

static DWORD WINAPI host_thread(LPVOID unused)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    int mouse_down = 0;
    (void)unused;
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
        fprintf(stderr, "host: SDL_Init failed: %s\n", SDL_GetError());
        SetEvent(ready);
        return 1;
    }
    {
        const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, KHDAYS_SOUND_RATE};
        SDL_AudioStream *stream =
            SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (stream == NULL) {
            fprintf(stderr, "host: no sound: %s\n", SDL_GetError());
        } else {
            SDL_ResumeAudioStreamDevice(stream);
            audio = stream;
        }
    }
    window = SDL_CreateWindow("Kingdom Hearts 358/2 Days (native)", 2 * W, 4 * H, SDL_WINDOW_RESIZABLE);
    renderer = window ? SDL_CreateRenderer(window, NULL) : NULL;
    texture = renderer ? SDL_CreateTexture(renderer, SDL_PIXELFORMAT_XRGB8888,
                                           SDL_TEXTUREACCESS_STREAMING, W, 2 * H)
                       : NULL;
    frame_event = SDL_RegisterEvents(1);
    if (texture == NULL || frame_event == 0) {
        fprintf(stderr, "host: cannot open the window: %s\n", SDL_GetError());
        SetEvent(ready);
        return 1;
    }
    SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST);
    SDL_SetRenderLogicalPresentation(renderer, W, 2 * H, SDL_LOGICAL_PRESENTATION_LETTERBOX);
    SDL_SetRenderVSync(renderer, 1);
    InterlockedExchange(&started, 1);
    SetEvent(ready);

    for (;;) {
        SDL_Event event;
        if (!SDL_WaitEvent(&event)) {
            continue;
        }
        switch (event.type) {
        case SDL_EVENT_QUIT:
            ExitProcess(0);
        case SDL_EVENT_KEY_DOWN:
        case SDL_EVENT_KEY_UP:
            read_keys();
            break;
        case SDL_EVENT_MOUSE_BUTTON_DOWN:
        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (event.button.button == SDL_BUTTON_LEFT) {
                mouse_down = event.type == SDL_EVENT_MOUSE_BUTTON_DOWN;
                read_mouse(renderer, event.button.x, event.button.y, mouse_down);
            }
            break;
        case SDL_EVENT_MOUSE_MOTION:
            if (mouse_down) {
                read_mouse(renderer, event.motion.x, event.motion.y, 1);
            }
            break;
        default:
            if (event.type == frame_event) {
                AcquireSRWLockShared(&frame_lock);
                SDL_UpdateTexture(texture, NULL, frame, W * 4);
                ReleaseSRWLockShared(&frame_lock);
                InterlockedExchange(&frame_pending, 0);
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                SDL_RenderTexture(renderer, texture, NULL, NULL);
                SDL_RenderPresent(renderer);
            }
            break;
        }
    }
}

int khdays_host_start(void)
{
    if (getenv("KHDAYS_HEADLESS") != NULL) {
        return 1;
    }
    ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    if (ready == NULL || CreateThread(NULL, 0, host_thread, NULL, 0, NULL) == NULL) {
        return 0;
    }
    WaitForSingleObject(ready, INFINITE);
    return started != 0;
}

void khdays_host_present(const uint32_t *upper, const uint32_t *lower)
{
    if (!started) {
        return;
    }
    AcquireSRWLockExclusive(&frame_lock);
    memcpy(frame, upper, W * H * 4);
    memcpy(frame + W * H, lower, W * H * 4);
    ReleaseSRWLockExclusive(&frame_lock);
    if (InterlockedExchange(&frame_pending, 1) == 0) {
        SDL_Event event;
        SDL_zero(event);
        event.type = frame_event;
        SDL_PushEvent(&event);
    }
}

/* The game makes sound as fast as the DS clock, which follows the PC's; the
 * device plays it on its own clock. What piles up beyond a tenth of a second
 * (after the game thread stalled and caught up) is dropped, so the delay
 * stays short. */
void khdays_host_audio(const int16_t *frames, int count)
{
    SDL_AudioStream *stream = audio;
    if (stream == NULL) {
        return;
    }
    if (SDL_GetAudioStreamQueued(stream) > KHDAYS_SOUND_RATE * 4 / 10) {
        return;
    }
    SDL_PutAudioStreamData(stream, frames, count * 4);
}

uint16_t khdays_host_buttons(void)
{
    return (uint16_t)buttons;
}

int khdays_host_touch(int *x, int *y)
{
    const LONG t = touch;
    if (!(t & 0x80000000)) {
        return 0;
    }
    *x = t & 0xff;
    *y = (t >> 8) & 0xff;
    return 1;
}
