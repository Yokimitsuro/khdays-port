/* KHDAYS_PROFILE: where the time of a second goes, by phase of the frame (the replacement models' posing,
 * cloth and skinning, the shadows, the 3D list's finish and interpolation, the renderer's steps), said
 * once a second in the log (events.c) beside the `wait:` line. Any thread may count; off, a phase costs
 * one test. */
#ifndef KHDAYS_PHASES_H
#define KHDAYS_PHASES_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum KhdaysPhase {
    KHDAYS_PHASE_MODEL_LOAD,    /* a replacement model read and set up (models.cpp load) */
    KHDAYS_PHASE_MODEL_POSE,    /* its bones posed for a draw, the cloth not counted */
    KHDAYS_PHASE_MODEL_CLOTH,   /* its secondary motion (apply_model_secondary) */
    KHDAYS_PHASE_MODEL_SKIN,    /* its vertices skinned, projected and handed to the 3D */
    KHDAYS_PHASE_HD_DECODE,     /* HD replacement pictures decoded (textures.c) */
    KHDAYS_PHASE_SHADOW_RECORD, /* the character shadows' captures (character_shadow.c) */
    KHDAYS_PHASE_SHADOW_FINISH, /* their receivers built and clipped */
    KHDAYS_PHASE_G3D_FINISH,    /* a 3D list clipped, sorted and put through the viewport */
    KHDAYS_PHASE_INTERP,        /* the 3D between two of the game's frames (g3d_interp.c) */
    KHDAYS_PHASE_RENDER_PREP,   /* the renderer's CPU work for a picture before its upload */
    KHDAYS_PHASE_RENDER_SUBMIT, /* its upload, passes recorded and submitted */
    KHDAYS_PHASE_COUNT
} KhdaysPhase;

/* Whether KHDAYS_PROFILE is set. */
int khdays_phases_on(void);
/* The counter now (0 when off): the start of a phase. */
unsigned long long khdays_phase_start(void);
/* Counts the time from `started` to now as one run of `phase`; _more adds it to the run already counted. */
void khdays_phase_end(KhdaysPhase phase, unsigned long long started);
void khdays_phase_more(KhdaysPhase phase, unsigned long long started);
/* Writes each phase's milliseconds and runs since the last call, and starts again (events.c, each second). */
void khdays_phases_second(void);

#ifdef __cplusplus
}

/* A phase from here to the end of the scope. */
struct KhdaysPhaseScope {
    KhdaysPhase phase;
    unsigned long long started;
    explicit KhdaysPhaseScope(KhdaysPhase p) : phase(p), started(khdays_phase_start()) {}
    ~KhdaysPhaseScope()
    {
        khdays_phase_end(phase, started);
    }
    KhdaysPhaseScope(const KhdaysPhaseScope &) = delete;
    KhdaysPhaseScope &operator=(const KhdaysPhaseScope &) = delete;
};
#endif

#endif
