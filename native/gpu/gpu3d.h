/* The DS's 3D engine (GBATEK, "DS 3D Video"): the geometry engine, fed
 * through GXFIFO and the command ports, and the rendering engine, which draws
 * the polygon list of the last SwapBuffers into the BG0 layer of engine A.
 *
 * What GBATEK documents is followed; where it leaves the arithmetic open (the
 * lighting's rounding, the depth value, how the rasterizer interpolates and
 * which pixels a polygon covers) the choice made here is marked, and the
 * result is close to the hardware, not bit-exact. */
#ifndef KHDAYS_GPU3D_H
#define KHDAYS_GPU3D_H

#include "vram_map.h"

#include <stdint.h>

void khdays_gpu3d_reset(void);

/* A 32-bit write to GXFIFO (0x04000400-0x0400043f). */
void khdays_gpu3d_fifo_write(uint32_t value);

/* A write to a command port (0x04000440-0x040005ff; `offset` from
 * 0x04000000): one parameter of that command, or the trigger of a command
 * without parameters. */
void khdays_gpu3d_port_write(uint32_t offset, uint32_t value);

/* GXSTAT written (0x04000600): the error acknowledge and the FIFO IRQ mode. */
void khdays_gpu3d_gxstat_write(uint32_t value);

/* The value of the 32-bit status/result register at `offset` (0x600-0x6a0:
 * GXSTAT, RAM_COUNT, POS_RESULT, VEC_RESULT, CLIPMTX_RESULT, VECMTX_RESULT). */
uint32_t khdays_gpu3d_read(uint32_t offset);

/* Whether the GXFIFO IRQ condition (GXSTAT bits 30-31) holds now. */
int khdays_gpu3d_irq(void);

/* VBlank begins: a pending SwapBuffers takes effect and the rendering engine
 * draws its polygon list (again, if none was swapped in) into `out`
 * (256x192: RGB 6 bits each in bits 0-5/8-13/16-21, alpha 0-31 in 24-28),
 * with the rendering registers from `io` (0x04000000) and textures from
 * `vram`. */
void khdays_gpu3d_vblank(const uint8_t *io, const KhdaysVramPages *vram, uint32_t *out);

#endif
