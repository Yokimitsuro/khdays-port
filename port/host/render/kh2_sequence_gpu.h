#ifndef KHDAYS_SEQUENCE_GPU_H
#define KHDAYS_SEQUENCE_GPU_H
#include <SDL3/SDL.h>
#include "../../gfx/sequence_image.h"
int khdays_sequence_gpu_draw(SDL_GPUDevice *device, SDL_GPUCommandBuffer *cmd, const KhdaysSequenceImage *image);
int khdays_sequence_gpu_open(SDL_GPUDevice *device);
void khdays_sequence_gpu_close(void);
/* Forgets what was drawn (its command buffer cancelled): every image is drawn again next time. */
void khdays_sequence_gpu_forget(void);
#endif
