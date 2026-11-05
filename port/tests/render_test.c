/* khdays-render-test [DIR]: recorded frame packages (KHDAYS_RECORD_FRAMES=saves/frames), checked
 * against the pictures the game showed and drawn again on the graphics card at 1x, 2x and 4x, then put
 * through the 3D interpolation; KHDAYS_RENDER_OUT=DIR writes what the GPU drew. Recordings hold the
 * player's data, so they live in saves/frames; without any the test is skipped (exit 77). What each
 * part checks and its limits: docs/BUILD.md, "The port's tests". */
#include "../host/render/gpu_raster3d.h"
#include "../gfx/g3d_interp.h"
#include "../runtime/display/frame_package.h"
#include "../runtime/mods/textures.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <string.h>

/* gpu_raster3d.c draws the textures mods put in the game's place
 * (runtime/mods/textures.c); the recordings draw the game's own. */
int khdays_textures_replacements(void)
{
    return 0;
}

const KhdaysTextureReplacement *khdays_textures_replacement(int id)
{
    (void)id;
    return NULL;
}

uint32_t khdays_textures_version(int id)
{
    (void)id;
    return 0;
}

void khdays_textures_read_begin(int id)
{
    (void)id;
}

void khdays_textures_read_end(int id)
{
    (void)id;
}

#define SKIPPED 77
#define MAX_FILES 4096

/* The limits of part 2 (see above). */
#define MAX_DIFFERING_AT_1X 0.0001 /* a share of the pixels where the 3D takes part */
#define MAX_MEAN_AT_NX 4.0         /* a colour channel, of 255 (see) */

static char names[MAX_FILES][64];
static int name_count;

static SDL_EnumerationResult SDLCALL collect(void *userdata, const char *dir, const char *name)
{
    (void)userdata;
    (void)dir;
    if (SDL_strncmp(name, "frame_", 6) == 0 && SDL_strstr(name, ".pkg") != NULL && name_count < MAX_FILES) {
        SDL_strlcpy(names[name_count++], name, sizeof(names[0]));
    }
    return SDL_ENUM_CONTINUE;
}

static int SDLCALL by_name(const void *a, const void *b)
{
    return SDL_strcmp((const char *)a, (const char *)b);
}

/* How far the GPU's picture is from the software's, over all packages at
 * one scale: `only2d` counts the target pixels where the 3D cannot show that
 * are not the 2D engine's; the rest is where the 3D takes part. */
typedef struct Distance {
    int scale, frames;
    long long only2d;
    long long pixels, differing, channel_sum;
    int channel_max;
} Distance;

static int channel_difference(uint32_t a, uint32_t b, long long *sum)
{
    int worst = 0;
    for (int shift = 0; shift < 24; shift += 8) {
        const int d = (int)((a >> shift) & 255) - (int)((b >> shift) & 255);
        const int abs = d < 0 ? -d : d;
        *sum += abs;
        worst = abs > worst ? abs : worst;
    }
    return worst;
}

static void measure(Distance *d, const uint32_t *gpu, int width, const KhdaysFramePackage *p)
{
    const int n = d->scale;
    for (int y = 0; y < KHDAYS_FRAME_H; ++y) {
        for (int x = 0; x < KHDAYS_FRAME_W; ++x) {
            const int i = y * KHDAYS_FRAME_W + x;
            const uint32_t software = p->screens[0][i];
            unsigned sum[3] = {0, 0, 0};
            for (int sy = 0; sy < n; ++sy) {
                for (int sx = 0; sx < n; ++sx) {
                    const uint32_t mine = gpu[(y * n + sy) * width + x * n + sx];
                    long long unused = 0;
                    if (p->composition[i].role == KHDAYS_2D_3D_HIDDEN) {
                        d->only2d += channel_difference(mine, software, &unused) != 0;
                    }
                    for (int c = 0; c < 3; ++c) {
                        sum[c] += (mine >> (8 * c)) & 255;
                    }
                }
            }
            if (p->composition[i].role != KHDAYS_2D_3D_HIDDEN) {
                const unsigned area = (unsigned)(n * n);
                const uint32_t average =
                    (sum[0] + area / 2) / area | ((sum[1] + area / 2) / area) << 8 | ((sum[2] + area / 2) / area) << 16;
                const int worst = channel_difference(average, software, &d->channel_sum);
                d->pixels++;
                d->differing += worst != 0;
                d->channel_max = worst > d->channel_max ? worst : d->channel_max;
            }
        }
    }
    d->frames++;
}

/* KHDAYS_RENDER_OUT: the GPU's picture as a BMP. */
static void write_picture(const char *out, uint32_t frame, int scale, const uint32_t *pixels, int w, int h)
{
    char path[1100];
    SDL_Surface *surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_XRGB8888, (void *)pixels, w * 4);
    SDL_snprintf(path, sizeof(path), "%s/frame_%05u_%dx.bmp", out, frame, scale);
    if (surface == NULL || !SDL_SaveBMP(surface, path)) {
        fprintf(stderr, "cannot write %s: %s\n", path, SDL_GetError());
    }
    SDL_DestroySurface(surface);
}

/* The list built again from the package's polygons before clipping
 * (gfx/g3d.h khdays_g3d_finish) is the package's list, byte for byte
 * (the DS's pace draws exactly today's list). */
static int finished_lists, finished_polygons;

static int finished_same(const KhdaysG3dList *l)
{
    static KhdaysG3dList rebuilt;
    if (!khdays_g3d_finish(l, NULL, &rebuilt)) {
        return 0;
    }
    ++finished_lists;
    finished_polygons += l->npolys;
    return rebuilt.npolys == l->npolys && rebuilt.nverts == l->nverts && rebuilt.beyond_ds == l->beyond_ds &&
           (l->npolys == 0 || memcmp(rebuilt.polys, l->polys, sizeof(KhdaysG3dPolygon) * (size_t)l->npolys) == 0) &&
           (l->nverts == 0 || memcmp(rebuilt.verts, l->verts, sizeof(KhdaysG3dVertex) * (size_t)l->nverts) == 0);
}

int main(int argc, char **argv)
{
    char dir[1024];
    int failures = 0, packages = 0, composed = 0, gpu = 0;
    Distance distance[3] = {{1, 0, 0, 0, 0, 0, 0}, {2, 0, 0, 0, 0, 0, 0}, {4, 0, 0, 0, 0, 0, 0}};
    SDL_GPUDevice *device = NULL;
    const char *out = SDL_getenv("KHDAYS_RENDER_OUT");
    KhdaysFramePackage *recorded = NULL; /* each package as recorded, then the rest zero */
    /* the list before, for the pictures between lists (part 3) */
    KhdaysG3dList before;
    uint32_t before_frame = 0, before_serial = 0;
    int have_before = 0, pairs = 0;
    long long pair_polygons = 0, pair_matched = 0, pair_jumped = 0;
    KhdaysG3dInterp interp;
    SDL_memset(&before, 0, sizeof(before));
    SDL_memset(&interp, 0, sizeof(interp));

    SDL_strlcpy(dir, argc > 1 ? argv[1] : "saves/frames", sizeof(dir));
    if (dir[0] != '\0' && dir[SDL_strlen(dir) - 1] != '/' && dir[SDL_strlen(dir) - 1] != '\\') {
        SDL_strlcat(dir, "/", sizeof(dir));
    }
    if (SDL_GetPathInfo(dir, NULL)) {
        SDL_EnumerateDirectory(dir, collect, NULL);
    }
    if (name_count == 0) {
        printf("no recorded frames in %s (record some with KHDAYS_RECORD_FRAMES); skipped\n", dir);
        return SKIPPED;
    }
    SDL_qsort(names, (size_t)name_count, sizeof(names[0]), by_name);
    recorded = (KhdaysFramePackage *)SDL_calloc(1, sizeof(*recorded));

    device =
        !SDL_Init(SDL_INIT_VIDEO)
            ? NULL
            : SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_DXIL | SDL_GPU_SHADERFORMAT_SPIRV | SDL_GPU_SHADERFORMAT_MSL,
                                  SDL_getenv("KHDAYS_GPU_DEBUG") != NULL, NULL);
    if (device != NULL && khdays_raster3d_open(device)) {
        gpu = 1;
        printf("graphics card: %s\n", SDL_GetGPUDeviceDriver(device));
    } else {
        printf("no graphics card to draw with (%s): the GPU part is skipped\n", SDL_GetError());
    }

    for (int f = 0; f < name_count; ++f) {
        char path[1100];
        size_t size = 0;
        uint32_t *data;
        const KhdaysFramePackage *p;
        const uint32_t both = KHDAYS_WANT_3D | KHDAYS_WANT_COMPOSITION;
        SDL_snprintf(path, sizeof(path), "%s%s", dir, names[f]);
        data = (uint32_t *)SDL_LoadFile(path, &size);
        if (data == NULL) {
            fprintf(stderr, "%s: cannot read: %s\n", path, SDL_GetError());
            ++failures;
            continue;
        }
        if (size < 16 + KHDAYS_FRAME_RECORDED || data[0] != 0x474b5046u || data[1] != 15 ||
            data[2] != KHDAYS_FRAME_RECORDED || recorded == NULL) {
            fprintf(stderr, "%s: not a frame package of this build (%u bytes)\n", path, (unsigned)size);
            ++failures;
            SDL_free(data);
            continue;
        }
        {
            /* the 3D list's polygons and vertices follow the package */
            /* what was recorded of it (all but the situation's inputs), then the rest */
            KhdaysFramePackage *q = recorded;
            uint8_t *after = (uint8_t *)(data + 4) + KHDAYS_FRAME_RECORDED;
            SDL_memcpy(q, data + 4, KHDAYS_FRAME_RECORDED);
            SDL_memset((uint8_t *)q + KHDAYS_FRAME_RECORDED, 0, sizeof(*q) - KHDAYS_FRAME_RECORDED);
            const size_t list_bytes = sizeof(KhdaysG3dPolygon) * (size_t)q->list3d.npolys +
                                      sizeof(KhdaysG3dVertex) * (size_t)q->list3d.nverts +
                                      (sizeof(KhdaysG3dPolygon) + sizeof(KhdaysG3dPreInfo)) * (size_t)q->list3d.npre +
                                      sizeof(KhdaysG3dPreVertex) * (size_t)q->list3d.npre_verts;
            if (q->list3d.npolys < 0 || q->list3d.nverts < 0 || q->list3d.npre < 0 || q->list3d.npre_verts < 0 ||
                size < 16 + KHDAYS_FRAME_RECORDED + list_bytes) {
                fprintf(stderr, "%s: its 3D list is not all there (%u bytes)\n", path, (unsigned)size);
                ++failures;
                SDL_free(data);
                continue;
            }
            q->list3d.polys = (KhdaysG3dPolygon *)after;
            q->list3d.verts = (KhdaysG3dVertex *)(after + sizeof(KhdaysG3dPolygon) * (size_t)q->list3d.npolys);
            q->list3d.poly_capacity = q->list3d.npolys;
            q->list3d.vert_capacity = q->list3d.nverts;
            after += sizeof(KhdaysG3dPolygon) * (size_t)q->list3d.npolys +
                     sizeof(KhdaysG3dVertex) * (size_t)q->list3d.nverts;
            q->list3d.pre = (KhdaysG3dPolygon *)after;
            after += sizeof(KhdaysG3dPolygon) * (size_t)q->list3d.npre;
            q->list3d.pre_info = (KhdaysG3dPreInfo *)after;
            after += sizeof(KhdaysG3dPreInfo) * (size_t)q->list3d.npre;
            q->list3d.pre_verts = (KhdaysG3dPreVertex *)after;
            q->list3d.pre_capacity = q->list3d.npre;
            q->list3d.pre_vert_capacity = q->list3d.npre_verts;
            after += sizeof(KhdaysG3dPreVertex) * (size_t)q->list3d.npre_verts;
            {
                /* the port's elements and their images (version 12) */
                const uint8_t *end = (const uint8_t *)data + size;
                int whole = 1;
                for (int e = 0; e < 2 && whole; ++e) {
                    const size_t bytes = (size_t)q->elements[e].count * KE_WORDS * sizeof(uint32_t);
                    whole = (size_t)(end - after) >= bytes;
                    q->elements[e].records = (uint32_t *)after;
                    q->elements[e].capacity = q->elements[e].count;
                    after += whole ? bytes : 0;
                }
                if (whole && (size_t)(end - after) >= 4) {
                    uint32_t images;
                    memcpy(&images, after, 4);
                    after += 4;
                    for (uint32_t k = 0; k < images && whole; ++k) {
                        uint32_t head[3];
                        whole = (size_t)(end - after) >= sizeof(head);
                        if (whole) {
                            memcpy(head, after, sizeof(head));
                            after += sizeof(head);
                            whole = (size_t)(end - after) >= (size_t)head[1] * head[2] * 4u;
                            after += whole ? (size_t)head[1] * head[2] * 4u : 0;
                        }
                    }
                } else {
                    whole = 0;
                }
                if (!whole || after != end) {
                    fprintf(stderr, "%s: its elements are not all there (%u bytes)\n", path, (unsigned)size);
                    ++failures;
                    SDL_free(data);
                    continue;
                }
            }
            p = q;
        }
        ++packages;
        if ((p->wants & KHDAYS_WANT_3D) && have_before && p->frame > before_frame && p->frame - before_frame <= 4 &&
            p->list3d_serial != before_serial) {
            KhdaysG3dList between;
            SDL_memset(&between, 0, sizeof(between));
            khdays_g3d_interpolate(&interp, &before, &p->list3d, 0.5f, &between);
            printf(
                "%s: between it and frame %u, %d of %d polygons matched, %d of them moved over a quarter of the view "
                "(%d a mod's model's)\n",
                names[f], before_frame, interp.matched, interp.polygons, interp.jumped, interp.jumped_mod);
            for (int d = 0; d < 4 && interp.jumped_in[d].count > 0; ++d) {
                int32_t in_draw = 0;
                for (int32_t i = 0; i < p->list3d.npre; ++i) {
                    in_draw += p->list3d.pre_info[i].draw == interp.jumped_in[d].draw &&
                               p->list3d.pre_info[i].occurrence == interp.jumped_in[d].occurrence;
                }
                printf("  %d of the %d polygons of draw %llx (occurrence %u)\n", interp.jumped_in[d].count, in_draw,
                       (unsigned long long)interp.jumped_in[d].draw, interp.jumped_in[d].occurrence);
            }
            ++pairs;
            pair_polygons += interp.polygons;
            pair_matched += interp.matched;
            pair_jumped += interp.jumped;
            khdays_g3d_list_free(&between);
        }
        if (p->wants & KHDAYS_WANT_3D) {
            /* kept for the next: its polygons before clipping */
            KhdaysG3dPolygon *pre =
                (KhdaysG3dPolygon *)SDL_realloc(before.pre, sizeof(KhdaysG3dPolygon) * (size_t)(p->list3d.npre + 1));
            KhdaysG3dPreInfo *info = (KhdaysG3dPreInfo *)SDL_realloc(before.pre_info, sizeof(KhdaysG3dPreInfo) *
                                                                                          (size_t)(p->list3d.npre + 1));
            KhdaysG3dPreVertex *verts = (KhdaysG3dPreVertex *)SDL_realloc(
                before.pre_verts, sizeof(KhdaysG3dPreVertex) * (size_t)(p->list3d.npre_verts + 1));
            if (pre != NULL)
                before.pre = pre;
            if (info != NULL)
                before.pre_info = info;
            if (verts != NULL)
                before.pre_verts = verts;
            have_before = pre != NULL && info != NULL && verts != NULL;
            if (have_before) {
                SDL_memcpy(before.pre, p->list3d.pre, sizeof(KhdaysG3dPolygon) * (size_t)p->list3d.npre);
                SDL_memcpy(before.pre_info, p->list3d.pre_info, sizeof(KhdaysG3dPreInfo) * (size_t)p->list3d.npre);
                SDL_memcpy(before.pre_verts, p->list3d.pre_verts,
                           sizeof(KhdaysG3dPreVertex) * (size_t)p->list3d.npre_verts);
                before.npre = before.pre_capacity = p->list3d.npre;
                before.npre_verts = before.pre_vert_capacity = p->list3d.npre_verts;
                before_frame = p->frame;
                before_serial = p->list3d_serial;
            }
        }
        if ((p->wants & KHDAYS_WANT_3D) && !finished_same(&p->list3d)) {
            fprintf(stderr, "%s: its 3D list built again from its polygons before clipping differs\n", path);
            ++failures;
        }
        if (p->frame != data[3] || p->upper > 1) {
            fprintf(stderr, "%s: the header and the package disagree\n", path);
            ++failures;
        } else if ((p->wants & both) == both && p->info[0].is3d) {
            int first;
            const int bad = khdays_frame_check_composition(p, p->layer3d, &first);
            ++composed;
            if (p->list3d.widen > 1.0f) {
                int beside = 0;
                for (int i = 0; i < p->list3d.npolys; ++i) {
                    const KhdaysG3dPolygon *poly = &p->list3d.polys[i];
                    int left = 1, right = 1;
                    for (int k = 0; k < poly->count; ++k) {
                        const float x = p->list3d.verts[poly->first + k].x;
                        left &= x < 0.0f;
                        right &= x >= (float)KHDAYS_FRAME_W;
                    }
                    beside += left || right;
                }
                printf("%s: %.0f DS columns wide, %d of its %d polygons wholly beside the DS's view\n", names[f],
                       p->list3d.widen * KHDAYS_FRAME_W, beside, p->list3d.npolys);
            }
            if (bad) {
                fprintf(stderr, "%s: %d pixels of engine A differ when composed again\n", path, bad);
                ++failures;
            }
            for (int s = 0; gpu && s < 3; ++s) {
                int w, h;
                uint32_t *pixels;
                if (!khdays_raster3d_draw(p, distance[s].scale, 0) || (pixels = khdays_raster3d_read(&w, &h)) == NULL) {
                    fprintf(stderr, "%s: the graphics card could not draw it at %dx: %s\n", path, distance[s].scale,
                            SDL_GetError());
                    ++failures;
                    continue;
                }
                if (p->list3d.widen <= 1.0f) {
                    measure(&distance[s], pixels, w, p); /* a wide one has no software picture to match */
                }
                if (out != NULL) {
                    write_picture(out, p->frame, distance[s].scale, pixels, w, h);
                }
                SDL_free(pixels);
            }
        }
        SDL_free(data);
    }

    printf("%d packages, %d composed again with the 3D, %d failed; %d 3D lists (%d polygons) built again "
           "from their polygons before clipping\n",
           packages, composed, failures, finished_lists, finished_polygons);
    if (pairs > 0) {
        printf(
            "between lists, %d pairs: %lld of %lld polygons matched, %lld of them moved over a quarter of the view\n",
            pairs, pair_matched, pair_polygons, pair_jumped);
    }
    SDL_free(before.pre);
    SDL_free(before.pre_info);
    SDL_free(before.pre_verts);
    khdays_g3d_interp_free(&interp);
    for (int s = 0; gpu && s < 3; ++s) {
        const Distance *d = &distance[s];
        if (d->frames == 0) {
            continue;
        }
        printf("GPU at %dx, %d frames: where the 3D takes part %.3f%% of pixels differ from the software's, "
               "a channel by %.3f on average and %d at most (of 255); where it cannot show, %lld pixels differ\n",
               d->scale, d->frames, 100.0 * (double)d->differing / (double)(d->pixels ? d->pixels : 1),
               (double)d->channel_sum / (double)(d->pixels ? d->pixels * 3 : 1), d->channel_max, d->only2d);
        if (d->only2d != 0) {
            fprintf(stderr, "GPU at %dx: the 2D where the 3D cannot show is not the 2D engine's\n", d->scale);
            ++failures;
        }
        if (d->pixels != 0 && d->scale == 1 && (double)d->differing / (double)d->pixels > MAX_DIFFERING_AT_1X) {
            fprintf(stderr, "GPU at 1x: more pixels differ than the limit (%.3f%%)\n", 100.0 * MAX_DIFFERING_AT_1X);
            ++failures;
        }
        if (d->pixels != 0 && d->scale > 1 && (double)d->channel_sum / (double)(d->pixels * 3) > MAX_MEAN_AT_NX) {
            fprintf(stderr, "GPU at %dx: the mean difference is beyond the limit (%.1f)\n", d->scale, MAX_MEAN_AT_NX);
            ++failures;
        }
    }
    if (gpu) {
        khdays_raster3d_close();
    }
    if (device != NULL) {
        SDL_DestroyGPUDevice(device);
    }
    SDL_free(recorded);
    return failures != 0;
}
