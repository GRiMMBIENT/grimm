/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's saves, the SLOOP way, in the six save sectors (platform.c save_flash: none in the sample area):
 *   SOUNDS (sector 0, SND_N slots of 64 B): a track's sound: its source, that source's four, ENV, LATCH, CHORD, LFO
 *   (a sound saved before the LFO: DEPTH 0), FM: its own patch (FM ALGO, FM OP; saved before: the PATCH as built);
 *   its CATEGORY
 *   and its number in it (the name: "DRONE 03"); saved before step 24 (128 slots of 32 B): made 64 of 64 B once at power-on, the first 64
 *   where they were, those past them into empty slots (snd_moved; no room: snd_lost). SND_FACTORY: the 40 factory sounds (in the image, read-only, their own
 *   names), first in PRESETS' lists; SAVE > SOUNDS lists, saves and clears the user's only
 *   the working project, saved by itself (autosave: grimm.c), in two objects, each in a pair of sectors written in
 *   turn (a cut save leaves the other copy):
 *     WORK  (sectors 1, 2): the four tracks (their source, every source's settings, ENV, LATCH, CHORD, ARP, LFO, LEVEL,
 *           the sends, PAN), the effects, the tempo, the SCALE, the four sequences, the slot in use, the song (its
 *           chain, MODE) and the mutes and solos, the tracks' FM patches (version 7; before: the PATCH as built)
 *   the slots A..D (each the four tracks, their sequences and step locks, their FM patches; tempo and effects are
 *   global): one copy, a stream over sectors 3, 4 and 5 (each its own header and CRC; the FM patches after the four
 *   slots, where it was zeros before step 24: zeros, the PATCH as built). Saving a slot rewrites the sectors it lives in
 *   only, each read back; a sector that fails its CRC at power-on: the slots in it empty (slot_lost: grimm.c says
 *   SLOT DATA LOST), the others and the working project as they were.
 *   At power-on the newest good copy of the working project loads: the FM-1 comes back as it was left. Version 6 (a
 *   step 4 B: its chord; the step locks); an older copy loads too (OBJ_VERS: 5, steps of 3 B, no locks; 4, before the
 *   song: an empty chain, nothing muted; 3, a track 48 B, before the LFO: LFO DEPTH 0; 2, 44 B, before ARP: ARP OFF as
 *   well), saved again as 6; the slots saved before version 6 (an object in sectors 3 / 4, two copies) load from
 *   their newest copy and are written in the stream (sector 5 first, then the older copy's, then the newer's: a cut
 *   in between leaves an old copy to load from again).
 *   undo / redo: one level, a snapshot of the tracks and sequences (and a slot, if the action changed one) taken before
 *   an action (grimm.c says which), restored on undo, the state before the undo kept for redo
 * Each record ends in its CRC-32: an erased sector, a cut save or another version is no record. A write erases a
 * sector (~50 ms of the audio stopped), then writes it a 256-byte page at a time: only while no voice sounds
 * (grimm.c). Main loop only (slot_apply_isr: the audio ISR, a recall at the bar). */

#define SND_N 64u                               /* sound slots: sector 0, 64 B each (128 of 32 B before step 24) */
#define SND_SIZE 64u
#define SND_MAGIC 0x54u                         /* "T" (32 B ones: "S") */
#define SND_MAGIC1 0x53u
#define SND_FSIZE 32u                           /* a factory sound's record (as the 32 B ones: no FM patch) */
#define SND_NAME 10u                            /* a user sound's name (the editor's): bytes 50..59, ASCII, 0-padded */
#define FM_PK 32u                               /* a track's FM patch, packed (FM_PK x NT: FM_ALL) */
#define FM_ALL (FM_PK * NT)
#define PK_ENG 7u                               /* a track's sources kept (NENG); bytes 29..31 (an eighth source's
                                                 * room till step 23): TRACK's filter and FX, 0 their defaults */
#define TRK_PK 52u                              /* a track, packed (OBJ_VERS: older versions' were shorter) */
#define SEQ_PK 260u                             /* a sequence, packed: 4 + 64 steps x 4 B (the 4th: its chord) */
#define SEQ_PK5 196u                            /* (version 5 and before: 3 B a step) */
#define LOCK_PK 288u                            /* a track's step locks: 64 x the four values, 64 x 4 bits which */
#define SLOT_LEN(tn, sq, lk) (4u + NT * ((tn) + (sq) + (lk)))   /* a slot: [0] used, its tracks, sequences, locks */
#define SONG_PK 20u                             /* the song: its chain (16), MODE, the mutes and solos, 2 spare */
#define WORK_LEN(tn, sg, sq, lk, fm) (NT * ((tn) + (sq) + (lk)) + 16u + 4u + (sg) + (fm))   /* the working project:
                                                 * tracks, effects, tempo, slot, .., sequences, locks, the song, the FM
                                                 * patches (an older version's sizes: sg 0 before the song, lk 0 before
                                                 * locks, fm 0 before the FM patches) */
#define SLOT_PK SLOT_LEN(TRK_PK, SEQ_PK, LOCK_PK)
#define SLOT_ALL (SLOT_PK + FM_ALL)             /* a slot in RAM: SLOT_PK, its FM patches after (the stream's tail) */
#define WORK_PK WORK_LEN(TRK_PK, SONG_PK, SEQ_PK, LOCK_PK, FM_ALL)
#define OBJ_HDR 12u                             /* magic, the save count, type, version, length */
#define OBJ_MAGIC 0x4B4D5247u                   /* "GRMK" */
#define OBJ_VER 7u
#define SLS_VER 6u                              /* the slots' stream (its layout as version 6's) */
#define NOBJV 6u
static const uint16_t OBJ_VERS[NOBJV][6] = {{7, 52, 20, 260, 288, FM_ALL}, {6, 52, 20, 260, 288, 0},
                                            {5, 52, 20, 196, 0, 0}, {4, 52, 0, 196, 0, 0}, {3, 48, 0, 196, 0, 0},
                                            {2, 44, 0, 196, 0, 0}};   /* the versions loaded, newest first: a track's,
                                                 * the song's, a sequence's, the locks', the FM patches' size (6: before
                                                 * the FM patches; 5: before chord steps and locks; 4: before the song;
                                                 * 3: before the LFO; 2: before ARP) */
#define SLS_MAGIC 0x534C5247u                   /* "GRLS": a sector of the slots' stream */
#define SLS_P (4096u - OBJ_HDR - 4u)            /* the stream's bytes a sector */
#define SLS_N 3u                                /* its sectors: 3, 4, 5 */
enum { OBJ_WORK, OBJ_SLOTS };
enum { CAT_DRONE, CAT_PAD, CAT_BELL, CAT_ORGAN, CAT_KEYS, CAT_BASS, CAT_TEXTURE, CAT_VOICE, CAT_OTHER, NCAT };
static const char *const CAT_NAME[NCAT] = {"DRONE", "PAD", "BELL", "ORGAN", "KEYS", "BASS", "TEXTURE", "VOICE", "OTHER"};
_Static_assert(NENG <= PK_ENG, "a save keeps every source's settings");
_Static_assert(SND_N * SND_SIZE <= 4096u && OBJ_HDR + WORK_PK + 4u <= 4096u && 4u * SLOT_ALL <= SLS_N * SLS_P &&
               SAVE_NSECT >= 3u + SLS_N, "the save sectors");

/* the factory sounds: GR!MM's own (first drafts), records as the user's (no CRC: in the image, read-only), first in
 * each CATEGORY's list; their names beside them. Bytes: CATEGORY, its number, the source, its four, ENV (ATTACK, DECAY,
 * SUSTAIN, RELEASE), LATCH, CHORD (chord mode off: VOICING << 4 | CHORD TYPE), LFO (RATE, DEPTH, DESTINATION, SHAPE).
 * Sources: 0 VA (WAVE 0 SAW, 1 SQUARE, 2 SUPERSAW), 1 FM (PATCH 0 BELL, 1 ORGAN, 2 SOFT PAD, 3 REED), 3 VOICE, 4 WHEEL,
 * 5 PHYS (MODEL 0 MODAL, 1 STRING, 2 MEMBRANE, 3 SYMPATH), 6 PHASE. LFO DESTINATION 0 PITCH, 1 VOLUME, 3 .. 6 KNOB 1 .. 4;
 * SHAPE 0 SINE, 1 TRIANGLE, 5 RANDOM, 6 DRIFT */
#define FSND(c, n, e, k0, k1, k2, k3, a, d, s, r, la, ch, lr, ld, lt, ls)                                              \
    {SND_MAGIC1, c, n, e, k0, k1, k2, k3, a, d, s, r, la, ch, lr, ld, lt, ls}
static const uint8_t SND_FACTORY[][SND_FSIZE] = {
    /* PADS */
    FSND(CAT_PAD, 1, ENG_VA, 2, 70, 62, 20, 105, 100, 110, 100, 1, 0x11, 30, 15, 5, 0),     /* CRYPT PAD */
    FSND(CAT_PAD, 2, ENG_VA, 0, 25, 50, 10, 95, 90, 115, 95, 1, 0x13, 35, 10, 5, 6),        /* ASH VEIL */
    FSND(CAT_PAD, 3, ENG_FM, 2, 90, 100, 60, 100, 90, 110, 105, 1, 0x27, 70, 15, 4, 1),     /* FROZEN HALL */
    FSND(CAT_PAD, 4, ENG_PHASE, 5, 0, 60, 8, 110, 100, 120, 100, 1, 0x06, 40, 10, 5, 0),    /* LAST LIGHT */
    FSND(CAT_PAD, 5, ENG_VA, 0, 45, 60, 25, 90, 90, 110, 95, 1, 0x04, 35, 1, 0, 6),         /* MOSS */
    FSND(CAT_PAD, 6, ENG_FM, 2, 40, 64, 30, 95, 90, 115, 100, 1, 0x11, 40, 0, 0, 0),        /* BLACK VELVET */
    /* DRONES */
    FSND(CAT_DRONE, 1, ENG_VA, 0, 15, 50, 0, 100, 90, 120, 105, 1, 0x05, 20, 8, 1, 0),      /* MONK DRONE */
    FSND(CAT_DRONE, 2, ENG_VA, 1, 5, 52, 30, 100, 90, 120, 110, 1, 0x05, 25, 30, 5, 0),     /* ABYSS */
    FSND(CAT_DRONE, 3, ENG_PHYS, 0, 100, 120, 60, 90, 90, 90, 100, 1, 0x05, 70, 20, 6, 5),  /* IRON LUNG */
    FSND(CAT_DRONE, 4, ENG_WHEEL, 0, 0, 10, 0, 70, 40, 60, 90, 1, 0x05, 15, 0, 1, 0),       /* TOMB HUM */
    FSND(CAT_DRONE, 5, ENG_VOICE, 96, 6, 100, 90, 90, 90, 115, 100, 1, 0x05, 115, 30, 1, 5), /* STATIC PRAYER */
    FSND(CAT_DRONE, 6, ENG_FM, 1, 50, 64, 12, 95, 90, 120, 100, 1, 0x05, 40, 0, 0, 0),      /* DEEP WELL */
    /* BELLS & METAL */
    FSND(CAT_BELL, 1, ENG_FM, 0, 64, 90, 10, 0, 120, 0, 110, 0, 0x00, 40, 0, 0, 0),         /* IRON BELL */
    FSND(CAT_BELL, 2, ENG_FM, 0, 80, 50, 0, 0, 100, 0, 95, 0, 0x00, 40, 0, 0, 0),           /* FUNERAL CHIME */
    FSND(CAT_BELL, 3, ENG_PHYS, 0, 110, 115, 0, 0, 90, 127, 110, 0, 0x00, 40, 0, 0, 0),     /* RUST GONG */
    FSND(CAT_BELL, 4, ENG_FM, 0, 85, 30, 20, 80, 100, 70, 95, 0, 0x00, 40, 0, 0, 0),        /* GLASS HARP */
    FSND(CAT_BELL, 5, ENG_PHYS, 0, 70, 110, 45, 85, 90, 90, 100, 0, 0x00, 40, 0, 0, 0),     /* BOWED BELL */
    FSND(CAT_BELL, 6, ENG_FM, 0, 110, 20, 30, 0, 70, 0, 70, 0, 0x00, 40, 0, 0, 0),          /* SHRINE */
    /* CHOIRS & VOICES */
    FSND(CAT_VOICE, 1, ENG_VOICE, 0, 14, 30, 40, 85, 90, 90, 95, 0, 0x12, 100, 1, 0, 0),    /* CHOIR OF GHOSTS */
    FSND(CAT_VOICE, 2, ENG_VOICE, 120, 4, 90, 45, 75, 80, 65, 90, 0, 0x05, 40, 0, 0, 0),    /* MONKS */
    FSND(CAT_VOICE, 3, ENG_VOICE, 20, 14, 20, 110, 80, 90, 115, 95, 0, 0x13, 35, 10, 3, 6), /* WHISPER CHORUS */
    FSND(CAT_VOICE, 4, ENG_VOICE, 64, 10, 45, 30, 90, 95, 65, 100, 0, 0x01, 25, 100, 3, 0), /* VIGIL */
    FSND(CAT_VOICE, 5, ENG_FM, 2, 70, 64, 40, 85, 90, 115, 95, 0, 0x12, 95, 1, 0, 0),       /* HOLLOW HYMN */
    /* ORGANS & KEYS */
    FSND(CAT_ORGAN, 1, ENG_WHEEL, 15, 0, 20, 10, 10, 20, 75, 60, 0, 0x00, 40, 0, 0, 0),     /* FUNERAL ORGAN */
    FSND(CAT_ORGAN, 2, ENG_WHEEL, 12, 0, 10, 0, 10, 90, 127, 55, 0, 0x00, 40, 0, 0, 0),     /* CHAPEL */
    FSND(CAT_KEYS, 1, ENG_FM, 0, 45, 10, 5, 0, 75, 0, 50, 0, 0x00, 40, 0, 0, 0),            /* DEAD HARPSICHORD */
    FSND(CAT_KEYS, 2, ENG_FM, 0, 90, 20, 0, 0, 75, 0, 60, 0, 0x00, 40, 0, 0, 0),            /* MUSIC BOX */
    FSND(CAT_KEYS, 3, ENG_PHASE, 3, 0, 30, 6, 0, 105, 20, 80, 0, 0x00, 40, 0, 0, 0),        /* CATHEDRAL KEYS */
    FSND(CAT_KEYS, 4, ENG_PHYS, 0, 30, 75, 0, 0, 90, 127, 60, 0, 0x00, 40, 0, 0, 0),        /* BONE PIANO */
    /* STRINGS & BRASS */
    FSND(CAT_OTHER, 1, ENG_PHYS, 1, 64, 110, 70, 100, 100, 65, 95, 0, 0x00, 40, 0, 0, 0),   /* HAUNTED STRINGS */
    FSND(CAT_OTHER, 2, ENG_VA, 0, 20, 70, 15, 65, 85, 105, 75, 0, 0x00, 100, 1, 0, 0),      /* DUNGEON BRASS */
    FSND(CAT_OTHER, 3, ENG_FM, 3, 85, 64, 15, 75, 90, 115, 85, 0, 0x00, 40, 0, 0, 0),       /* WAR HORN */
    FSND(CAT_OTHER, 4, ENG_PHYS, 1, 50, 115, 60, 85, 95, 70, 95, 0, 0x00, 95, 1, 0, 0),     /* CELLO GRAVE */
    FSND(CAT_OTHER, 5, ENG_VA, 2, 40, 75, 5, 90, 90, 115, 95, 0, 0x00, 70, 1, 0, 0),        /* SORROW STRINGS */
    /* LEADS & PLUCKS */
    FSND(CAT_KEYS, 5, ENG_PHASE, 6, 0, 90, 0, 30, 90, 110, 80, 0, 0x00, 105, 1, 0, 0),      /* WRAITH */
    FSND(CAT_KEYS, 6, ENG_VA, 1, 6, 55, 10, 40, 90, 110, 75, 0, 0x00, 40, 0, 0, 0),         /* LANTERN */
    FSND(CAT_KEYS, 7, ENG_FM, 0, 40, 10, 0, 0, 55, 0, 30, 0, 0x00, 40, 0, 0, 0),            /* SPIDER PLUCK */
    FSND(CAT_KEYS, 8, ENG_PHYS, 3, 0, 85, 0, 0, 90, 127, 60, 0, 0x00, 40, 0, 0, 0),         /* LUTE */
    FSND(CAT_KEYS, 9, ENG_PHASE, 3, 0, 40, 4, 80, 90, 70, 80, 0, 0x00, 100, 1, 0, 0),       /* CANDLE */
    /* TEXTURES */
    FSND(CAT_TEXTURE, 1, ENG_VOICE, 96, 12, 0, 127, 100, 90, 120, 105, 1, 0x00, 30, 90, 4, 0), /* WIND THRU STONE */
};
static const char *const SND_FNAME[] = {
    "CRYPT PAD", "ASH VEIL", "FROZEN HALL", "LAST LIGHT", "MOSS", "BLACK VELVET",
    "MONK DRONE", "ABYSS", "IRON LUNG", "TOMB HUM", "STATIC PRAYER", "DEEP WELL",
    "IRON BELL", "FUNERAL CHIME", "RUST GONG", "GLASS HARP", "BOWED BELL", "SHRINE",
    "CHOIR OF GHOSTS", "MONKS", "WHISPER CHORUS", "VIGIL", "HOLLOW HYMN",
    "FUNERAL ORGAN", "CHAPEL", "DEAD HARPSICHORD", "MUSIC BOX", "CATHEDRAL KEYS", "BONE PIANO",
    "HAUNTED STRINGS", "DUNGEON BRASS", "WAR HORN", "CELLO GRAVE", "SORROW STRINGS",
    "WRAITH", "LANTERN", "SPIDER PLUCK", "LUTE", "CANDLE",
    "WIND THRU STONE",
};   /* (16 characters at most: the header beside HOME's tempo) */
#define SND_NFACT (sizeof SND_FACTORY / sizeof SND_FACTORY[0])
_Static_assert(SND_NFACT == sizeof SND_FNAME / sizeof SND_FNAME[0], "a name a factory sound");
static const char *snd_fname(const uint8_t *r)  /* a factory sound's name, or 0 (the user's) */
{
    uintptr_t a = (uintptr_t)r, b = (uintptr_t)SND_FACTORY[0];
    return a >= b && a < b + sizeof SND_FACTORY ? SND_FNAME[(a - b) / SND_FSIZE] : 0;
}

static uint8_t st_img[4096] __attribute__((section(".pool")));   /* a sector as it will be written */
static uint32_t st_gen;                         /* + 1 a save (the screens: what they list has changed) */
static uint8_t slots[4][SLOT_ALL];              /* A..D, packed ([0]: used), each its FM patches after SLOT_PK */
static uint32_t slot_dirty, slot_lost, slot_up; /* slots to write (autosave); lost at power-on (bit: slot); slot_up:
                                                 * the old copies' newest sector + 1, an upgrade to write */
static volatile int32_t slot_cur;               /* the slot in use (the last saved or recalled) */
static uint32_t st_crc(const uint8_t *p, uint32_t n) { return ~smp_crc(0xFFFFFFFFu, p, n); }
static uint32_t st_rd32(const uint8_t *p) { return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static void st_wr32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static int st_write(uint32_t s, const uint8_t *img)   /* sector s := img (4 KiB) */
{
    st_gen++;
    if (save_flash_erase(s)) return -2;
    return save_flash_write(s, 0, img, 4096u) ? -3 : 0;
}

/* a track's CHORD in a byte (0: OFF, SCALE, CLOSE: as a save from before it): ON 0x80, VOICING << 4, CHORD TYPE */
static uint8_t ch_pack(uint32_t t)
{
    return (uint8_t)((trk[t].chord ? 0x80u : 0u) | (uint32_t)clamp(trk[t].ch_voice, 0, NVOICING - 1) << 4 |
                     (uint32_t)clamp(trk[t].ch_type, 0, NCHORD - 1));
}
static void ch_unpack(uint32_t t, uint32_t b)
{
    trk[t].ch_type = clamp((int32_t)(b & 15u), 0, NCHORD - 1);
    trk[t].ch_voice = clamp((int32_t)(b >> 4 & 7u), 0, NVOICING - 1);
    trk[t].chord = (int32_t)(b >> 7 & 1u);
}

/* a track's LFO in 4 bytes: RATE, DEPTH, DESTINATION, SHAPE (all 0: DEPTH 0, as a save from before it) */
static void lfo_pk(uint8_t *q, uint32_t t)
{
    q[0] = (uint8_t)clamp(trk[t].lfo_rate, 0, 127), q[1] = (uint8_t)clamp(trk[t].lfo_depth, 0, 100);
    q[2] = (uint8_t)clamp(trk[t].lfo_dest, 0, NLFODEST - 1), q[3] = (uint8_t)clamp(trk[t].lfo_shape, 0, NLFOSHAPE - 1);
}
static void lfo_uk(const uint8_t *q, uint32_t t)
{
    trk[t].lfo_rate = clamp(q[0], 0, 127), trk[t].lfo_depth = clamp(q[1], 0, 100);
    trk[t].lfo_dest = clamp(q[2], 0, NLFODEST - 1), trk[t].lfo_shape = clamp(q[3], 0, NLFOSHAPE - 1);
}

/* a track's FM patch in FM_PK bytes: ALGO + 0x80 (0: none, the PATCH as built), FEEDBACK, a row an operator (OP1
 * first): RATIO, FINE, LEVEL, ATTACK, DECAY */
static void pk_fm(uint8_t *q, uint32_t t)
{
    uint32_t i, k;
    q[0] = (uint8_t)(0x80u | (uint32_t)clamp(trk[t].fm_alg, 0, 31)), q[1] = (uint8_t)clamp(trk[t].fm_fb, 0, 7);
    for (i = 0; i < 6u; i++)
        for (k = 0; k < 5u; k++) q[2u + i * 5u + k] = (uint8_t)clamp(trk[t].fmo[i][k], 0, k == FO_RATIO ? 31 : 99);
}
static void uk_fm(const uint8_t *q, uint32_t t) /* (q 0 or none in it: the track's PATCH as built) */
{
    uint32_t i, k;
    if (!q || !(q[0] & 0x80u)) { fm_builtin(t, trk[t].p[ENG_FM][0]); return; }
    trk[t].fm_alg = q[0] & 31u, trk[t].fm_fb = q[1] & 7u;
    for (i = 0; i < 6u; i++)
        for (k = 0; k < 5u; k++) trk[t].fmo[i][k] = clamp(q[2u + i * 5u + k], 0, k == FO_RATIO ? 31 : 99);
}

/* ---- the sounds */
static const uint8_t *snd_ok(const uint8_t *f)
{
    return f && f[0] == SND_MAGIC && f[1] < NCAT && f[3] < NENG && st_rd32(f + 60) == st_crc(f, 60) ? f : 0;
}
static const uint8_t *snd_ok1(const uint8_t *f) /* a 32 B record (saved before step 24), or 0 */
{
    return f[0] == SND_MAGIC1 && f[1] < NCAT && f[3] < NENG && st_rd32(f + 28) == st_crc(f, 28) ? f : 0;
}
static uint32_t snd_moved, snd_lost;            /* (power-on, the upgrade: sounds past slot 64 moved down; no room) */
static void snd_upgrade(void)                   /* sector 0 as saved before step 24: as 64 records of 64 B (a sound
                                                 * at slot i < 64 stays there; one past them into the first empty one) */
{
    const uint8_t *f = save_flash(0);
    uint32_t i, j, k, any = 0;
    if (!f) return;
    for (i = 0; i < 128u; i++) any |= snd_ok1(f + i * 32u) != 0;
    if (!any) return;
    for (k = 0; k < 4096u; k++) st_img[k] = 0xFFu;
    for (i = 0; i < 128u; i++) {
        uint8_t *d;
        if (!snd_ok1(f + i * 32u)) continue;
        j = i;
        if (i >= SND_N)                         /* (past them: the first slot empty, nothing below 64 to come there) */
            for (j = 0; j < SND_N && (st_img[j * SND_SIZE] != 0xFFu || snd_ok1(f + j * 32u)); j++)
                ;
        if (j >= SND_N) { snd_lost++; continue; }
        snd_moved += i != j;
        d = st_img + j * SND_SIZE;
        for (k = 0; k < SND_SIZE; k++) d[k] = k < 18u ? f[i * 32u + k] : 0u;
        d[0] = SND_MAGIC;
        st_wr32(d + 60, st_crc(d, 60));
    }
    st_write(0, st_img);
}
static const uint8_t *snd_rec(uint32_t i)       /* slot i's record, or 0 (empty) */
{
    const uint8_t *f = save_flash(0);
    return f && i < SND_N ? snd_ok(f + i * SND_SIZE) : 0;
}
static char *snd_rec_name(const uint8_t *r, char *s)   /* "DRONE 03"; its name from the editor (bytes 50..59), if it
                                                 * has one; a factory sound's: its name */
{
    uint32_t k, n = 0;
    *s = 0;
    if (!r) return s;
    if (snd_fname(r)) return put_s(s, snd_fname(r));
    for (k = 0; k < SND_NAME; k++)              /* (the name: what the font has, else a space; to its last letter) */
        if (r[50 + k] > 32u && r[50 + k] < 127u) n = k + 1u;
    if (n) {
        for (k = 0; k < n; k++) *s++ = r[50 + k] >= 32u && r[50 + k] < 127u ? (char)r[50 + k] : ' ';
        *s = 0;
        return s;
    }
    s = put_s(put_s(s, CAT_NAME[r[1]]), " ");
    *s++ = (char)('0' + r[2] / 10u % 10u), *s++ = (char)('0' + r[2] % 10u), *s = 0;
    return s;
}
static char *snd_name(uint32_t i, char *s) { return snd_rec_name(snd_rec(i), s); }
static int snd_put(uint32_t i, const uint8_t *rec)   /* slot i := rec (0: cleared): its sector rewritten */
{
    const uint8_t *f = save_flash(0);
    uint32_t k;
    if (!f || i >= SND_N) return -1;
    for (k = 0; k < 4096u; k++) st_img[k] = f[k];
    for (k = 0; k < SND_SIZE; k++) st_img[i * SND_SIZE + k] = rec ? rec[k] : 0xFFu;
    return st_write(0, st_img);
}
static void snd_pack(uint8_t *r, uint32_t t, uint32_t cat, uint32_t num)   /* track t's sound as a record */
{
    uint32_t k, e = (uint32_t)clamp(trk[t].eng, 0, NENG - 1);
    for (k = 0; k < SND_SIZE; k++) r[k] = 0;
    r[0] = SND_MAGIC, r[1] = (uint8_t)cat, r[2] = (uint8_t)num, r[3] = (uint8_t)e;
    if (e == ENG_FM) pk_fm(r + 18, t);          /* (FM: its own patch) */
    for (k = 0; k < 4u; k++) {
        r[4 + k] = (uint8_t)clamp(trk[t].p[e][k], 0, 255);
        r[8 + k] = (uint8_t)clamp(trk[t].env[k], 0, 127);
    }
    r[12] = (uint8_t)(trk[t].latch != 0);
    r[13] = ch_pack(t);
    lfo_pk(r + 14, t);
    st_wr32(r + 60, st_crc(r, 60));
}
static int snd_save(uint32_t i, uint32_t t, uint32_t cat)   /* track t's sound into slot i, in CATEGORY cat (the name
                                                 * of the sound it replaces kept) */
{
    uint8_t r[SND_SIZE];
    uint32_t k, num = 0;
    const uint8_t *was = snd_rec(i);
    for (k = 0; k < SND_N; k++) {               /* its number: one past the category's highest */
        const uint8_t *o = snd_rec(k);
        if (o && k != i && o[1] == cat && o[2] > num) num = o[2];
    }
    snd_pack(r, t, cat, num + 1u > 99u ? 99u : num + 1u);
    if (was) {
        for (k = 0; k < SND_NAME; k++) r[50 + k] = was[50 + k];
        st_wr32(r + 60, st_crc(r, 60));
    }
    return snd_put(i, r);
}
static int snd_clear(uint32_t i) { return snd_put(i, 0); }
static void snd_apply(const uint8_t *r, uint32_t t)   /* a sound record into track t (a new source: its notes go over) */
{
    uint32_t k, e = r[3];
    for (k = 0; k < 4u; k++) {
        trk[t].p[e][k] = r[4 + k];
        trk[t].env[k] = r[8 + k];
    }
    if (trk[t].latch && !r[12]) nq_put(NQ_RELEASE, t, 0);   /* (LATCH ON -> OFF: its latched notes released) */
    trk[t].latch = r[12];
    ch_unpack(t, r[13]);
    lfo_uk(r + 14, t);                          /* (saved before the LFO: zeros, DEPTH 0) */
    if (e == ENG_FM) uk_fm(snd_fname(r) ? 0 : r + 18, t);   /* (FM: its patch; a factory sound's: the PATCH as built) */
    if ((uint32_t)trk[t].eng != e) {
        trk[t].eng = (int32_t)e;
        nq_put(NQ_RESTART, t, 0);
    }
}
static int snd_load(uint32_t i, uint32_t t)
{
    const uint8_t *r = snd_rec(i);
    if (!r) return -1;
    snd_apply(r, t);
    return 0;
}
/* PRESETS' list of CATEGORY c: its factory sounds, then the user's in slot order; its k-th record (0: past the end) */
static const uint8_t *cat_item(uint32_t c, uint32_t k)
{
    uint32_t i, n = 0;
    for (i = 0; i < SND_NFACT; i++)
        if (SND_FACTORY[i][1] == c && n++ == k) return SND_FACTORY[i];
    for (i = 0; i < SND_N; i++) {
        const uint8_t *r = snd_rec(i);
        if (r && r[1] == c && n++ == k) return r;
    }
    return 0;
}
static uint32_t cat_len(uint32_t c)
{
    uint32_t n = 0;
    while (cat_item(c, n)) n++;
    return n;
}

/* ---- packing: a track (52 B), a sequence (260 B), a track's step locks (288 B) */
static uint8_t *pk_track(uint8_t *q, uint32_t t)
{
    uint32_t e, k;
    for (k = 0; k < TRK_PK; k++) q[k] = 0;
    q[0] = (uint8_t)clamp(trk[t].eng, 0, NENG - 1);
    for (e = 0; e < NENG; e++)
        for (k = 0; k < 4u; k++) q[1 + e * 4u + k] = (uint8_t)clamp(trk[t].p[e][k], 0, 255);
    for (k = 0; k < 4u; k++) q[33 + k] = (uint8_t)clamp(trk[t].env[k], 0, 127);
    q[29] = (uint8_t)(127 - clamp(trk[t].cut, 0, 127)), q[30] = (uint8_t)clamp(trk[t].res, 0, 127);   /* TRACK (0, 0, 0:
                                                 * open, 0, 100 %: as a save from before it) */
    q[31] = (uint8_t)(int8_t)(clamp(trk[t].fx, 0, 200) - 100);
    q[37] = (uint8_t)(trk[t].latch != 0);
    q[38] = (uint8_t)clamp(trk[t].level, 0, 127), q[39] = (uint8_t)clamp(trk[t].dly, 0, 127);
    q[40] = (uint8_t)clamp(trk[t].rev, 0, 127), q[41] = (uint8_t)clamp(trk[t].grn, 0, 127);
    q[42] = (uint8_t)clamp(trk[t].pan, 0, 127);
    q[43] = ch_pack(t);
    q[44] = (uint8_t)((trk[t].arp ? 0x80u : 0u) | (uint32_t)clamp(trk[t].arp_mode, 0, NARPMODE - 1));   /* ARP */
    q[45] = (uint8_t)clamp(trk[t].arp_rate, 0, NARPRATE - 1), q[46] = (uint8_t)clamp(trk[t].arp_oct, 1, 4);
    q[47] = (uint8_t)clamp(trk[t].arp_gate, 10, 100);
    lfo_pk(q + 48, t);                          /* LFO */
    return q + TRK_PK;
}
static void trk_def(uint8_t *q, uint32_t n)     /* a track's bytes from n on (a save before ARP, before the LFO) as at
                                                 * power-on: ARP OFF (UP, 1/16, 1 octave, 50 %), LFO DEPTH 0 */
{
    static const track_t T0 = TRK_DEF;
    uint8_t d[TRK_PK];
    uint32_t k;
    d[44] = (uint8_t)T0.arp_mode, d[45] = (uint8_t)T0.arp_rate, d[46] = (uint8_t)T0.arp_oct, d[47] = (uint8_t)T0.arp_gate;
    d[48] = (uint8_t)T0.lfo_rate, d[49] = 0, d[50] = (uint8_t)T0.lfo_dest, d[51] = (uint8_t)T0.lfo_shape;
    for (k = n < 44u ? 44u : n; k < TRK_PK; k++) q[k] = d[k];
}
static void restart_track(uint32_t t);          /* (grimm.c: the audio ISR's NQ_RESTART, */
static void release_keys(uint32_t t);           /* NQ_RELEASE) */
static const uint8_t *uk_track(const uint8_t *q0, uint32_t t, int isr, uint32_t n)   /* isr: from the audio ISR (no
                                                 * asks); n: its size (an older version's: shorter) */
{
    uint8_t q[TRK_PK];
    uint32_t e, k;
    for (k = 0; k < TRK_PK && k < n; k++) q[k] = q0[k];
    trk_def(q, n);                              /* (what an older save has not: as at power-on) */
    for (e = 0; e < NENG; e++)
        for (k = 0; k < 4u; k++) trk[t].p[e][k] = q[1 + e * 4u + k];
    for (k = 0; k < 4u; k++) trk[t].env[k] = q[33 + k];
    if (trk[t].latch && !q[37]) {               /* (LATCH ON -> OFF: its latched notes released; at the bar, now) */
        if (!isr) nq_put(NQ_RELEASE, t, 0);
        else {
            release_keys(t);
            if (trk[t].arp) arp_clear(t);
        }
    }
    trk[t].latch = q[37];
    trk[t].level = q[38], trk[t].dly = q[39], trk[t].rev = q[40], trk[t].grn = q[41], trk[t].pan = q[42];
    trk[t].cut = 127 - clamp(q[29], 0, 127), trk[t].res = clamp(q[30], 0, 127), trk[t].fx = clamp((int8_t)q[31] + 100, 0, 200);
    ch_unpack(t, q[43]);
    trk[t].arp_mode = clamp(q[44] & 0x7Fu, 0, NARPMODE - 1), trk[t].arp_rate = clamp(q[45], 0, NARPRATE - 1);
    trk[t].arp_oct = clamp(q[46], 1, 4), trk[t].arp_gate = clamp(q[47], 10, 100);
    trk[t].arp = (int32_t)(q[44] >> 7 & 1u);    /* (on or off: arp.c's arp_watch starts it empty) */
    lfo_uk(q + 48, t);
    if (trk[t].eng != (int32_t)q[0] && q[0] < NENG) {
        trk[t].eng = q[0];
        if (isr) restart_track(t);
        else nq_put(NQ_RESTART, t, 0);
    }
    return q0 + n;
}
static uint8_t *pk_seq(uint8_t *q, uint32_t t)
{
    const seq_pat_t *P = &seq[t].pat[0];
    uint32_t k;
    q[0] = (uint8_t)clamp(seq[t].steps, 1, SEQ_N), q[1] = (uint8_t)clamp(seq[t].div, 0, 7);
    q[2] = (uint8_t)clamp(seq[t].swing, 0, 127), q[3] = (uint8_t)clamp(seq[t].mode, 0, SEQ_NMODE - 1);
    for (k = 0; k < SEQ_N; k++) {               /* a step: NOTE 7, CHANCE 7, RATCHET - 1 2, LENGTH - 1 6 bits; its chord */
        uint32_t v = (uint32_t)clamp(P->st[k].note, 0, 127) | (uint32_t)clamp(P->st[k].chance, 0, 100) << 7 |
                     (uint32_t)(clamp(P->st[k].ratchet, 1, 4) - 1) << 14 | (uint32_t)(clamp(P->st[k].len, 1, SEQ_N) - 1) << 16;
        q[4 + k * 4u] = (uint8_t)v, q[5 + k * 4u] = (uint8_t)(v >> 8), q[6 + k * 4u] = (uint8_t)(v >> 16);
        q[7 + k * 4u] = (uint8_t)P->st[k].chord;
    }
    return q + SEQ_PK;
}
static const uint8_t *uk_seq(const uint8_t *q, uint32_t t, uint32_t sq)   /* (sq: SEQ_PK5, steps of 3 B, no chords) */
{
    seq_pat_t *P = &seq[t].pat[0];
    uint32_t k, w = sq == SEQ_PK ? 4u : 3u;
    seq[t].steps = clamp(q[0], 1, SEQ_N), seq[t].div = clamp(q[1], 0, 7);
    seq[t].swing = clamp(q[2], 0, 127), seq[t].mode = clamp(q[3], 0, SEQ_NMODE - 1);
    for (k = 0; k < SEQ_N; k++) {
        const uint8_t *b = q + 4 + k * w;
        uint32_t v = b[0] | (uint32_t)b[1] << 8 | (uint32_t)b[2] << 16;
        P->st[k].note = (int32_t)(v & 127u), P->st[k].chance = clamp((int32_t)(v >> 7 & 127u), 0, 100);
        P->st[k].ratchet = (int32_t)(v >> 14 & 3u) + 1, P->st[k].len = (int32_t)(v >> 16 & 63u) + 1;
        P->st[k].chord = w == 4u && (b[3] & 0x80u) ? b[3] : 0;
    }
    return q + sq;
}
static uint8_t *pk_lock(uint8_t *q, uint32_t t) /* the step locks: a step's four values, then a nibble a step: which */
{
    const seq_pat_t *P = &seq[t].pat[0];
    uint32_t k, j;
    for (k = 0; k < SEQ_N; k++)
        for (j = 0; j < 4u; j++) q[k * 4u + j] = P->st[k].lv[j];
    for (k = 0; k < SEQ_N / 2u; k++)
        q[SEQ_N * 4u + k] = (uint8_t)((P->st[2u * k].lmask & 15u) | (P->st[2u * k + 1u].lmask & 15u) << 4);
    return q + LOCK_PK;
}
static const uint8_t *uk_lock(const uint8_t *q, uint32_t t, uint32_t lk)   /* (lk 0: saved before locks, none) */
{
    seq_pat_t *P = &seq[t].pat[0];
    uint32_t k, j;
    for (k = 0; k < SEQ_N; k++) {
        for (j = 0; j < 4u; j++) P->st[k].lv[j] = lk ? q[k * 4u + j] : 0;
        P->st[k].lmask = lk ? (uint8_t)(q[SEQ_N * 4u + k / 2u] >> (k & 1u ? 4 : 0) & 15u) : 0;
    }
    return q + lk;
}

/* ---- the working project: tracks, effects, tempo, the slot in use, sequences (WORK_PK) */
static volatile int32_t *const PK_FX[16] = {&tf_wow, &tf_sat, &tf_tone, &tf_hiss, &ge_size, &ge_dens, &ge_scat, &ge_mix,
                                            &dl_time, &dl_fb, &dl_wear, &dl_mix, &rv_size, &rv_damp, &rv_mix, &rv_pre};
static void pk_work(uint8_t *q)
{
    uint32_t t, k;
    for (t = 0; t < NT; t++) q = pk_track(q, t);
    for (k = 0; k < 16u; k++) *q++ = (uint8_t)clamp(*PK_FX[k], 0, 127);
    q[0] = (uint8_t)clamp(seq_bpm, 20, 300), q[1] = (uint8_t)(clamp(seq_bpm, 20, 300) >> 8);
    q[2] = (uint8_t)(clamp(slot_cur, 0, 3) | clamp(sc_root, 0, 11) << 4);   /* (and SCALE's ROOT, */
    q[3] = (uint8_t)((sel & 3u) | (uint32_t)clamp(sc_scale, 0, NSCALE - 1) << 4);   /* the track selected, SCALE) */
    q += 4;
    for (t = 0; t < NT; t++) q = pk_seq(q, t);
    for (t = 0; t < NT; t++) q = pk_lock(q, t);
    for (k = 0; k < SONG_N; k++)                /* the song: a step a byte (its slot or END, its REPEATS - 1 << 3), */
        q[k] = (uint8_t)(clamp(song_pat[k], 0, SONG_END) | (clamp(song_rep[k], 1, 16) - 1) << 3);
    q[16] = (uint8_t)clamp(song_mode, 0, NSONGMODE - 1);   /* MODE, */
    q[17] = (uint8_t)((trk_mute & 15u) | (trk_solo & 15u) << 4), q[18] = q[19] = 0;   /* the mutes and solos */
    for (t = 0; t < NT; t++) pk_fm(q + SONG_PK + t * FM_PK, t);   /* the FM patches */
}
enum { UW_GLOBALS = 1, UW_CHAIN = 2 };
static void uk_work(const uint8_t *q, uint32_t f, const uint16_t *vs)   /* (f: UW_GLOBALS, the effects, tempo, slot,
                                                 * SCALE, MODE, mutes too; UW_CHAIN, the song's chain too; vs: its
                                                 * version's row of OBJ_VERS, the sizes) */
{
    uint32_t t, k, tn = vs[1], sg = vs[2], sq = vs[3], lk = vs[4], fm = vs[5];
    int globals = (f & UW_GLOBALS) != 0;
    for (t = 0; t < NT; t++) q = uk_track(q, t, 0, tn);
    if (globals) {
        for (k = 0; k < 16u; k++) *PK_FX[k] = q[k];
        seq_bpm = clamp(q[16] | q[17] << 8, 20, 300);
        slot_cur = q[18] & 3u;
        sel = q[19] & 3u;
        sc_root = clamp(q[18] >> 4, 0, 11), sc_scale = clamp(q[19] >> 4, 0, NSCALE - 1);
    }
    q += 20;
    for (t = 0; t < NT; t++) q = uk_seq(q, t, sq);
    for (t = 0; t < NT; t++) q = uk_lock(q, t, lk);
    for (t = 0; t < NT; t++) uk_fm(fm ? q + sg + t * FM_PK : 0, t);   /* (saved before them: the PATCH as built) */
    if (!sg) return;                            /* (saved before the song: as it is) */
    if (f & UW_CHAIN)
        for (k = 0; k < SONG_N; k++) song_pat[k] = clamp(q[k] & 7u, 0, SONG_END), song_rep[k] = (q[k] >> 3 & 15u) + 1u;
    if (globals) {
        song_mode = clamp(q[16], 0, NSONGMODE - 1);
        trk_mute = q[17] & 15u, trk_solo = q[17] >> 4 & 15u;
    }
}
static void pk_slot(uint8_t *q)                 /* the four tracks, their sequences and locks into a slot (used) */
{
    uint32_t t;
    q[0] = 1, q[1] = q[2] = q[3] = 0;
    q += 4;
    for (t = 0; t < NT; t++) q = pk_track(q, t);
    for (t = 0; t < NT; t++) q = pk_seq(q, t);
    for (t = 0; t < NT; t++) q = pk_lock(q, t);
    for (t = 0; t < NT; t++) pk_fm(q + t * FM_PK, t);   /* (its FM patches: SLOT_PK on) */
}
static void uk_slot(const uint8_t *q, int isr)
{
    uint32_t t;
    q += 4;
    for (t = 0; t < NT; t++) q = uk_track(q, t, isr, TRK_PK);
    for (t = 0; t < NT; t++) q = uk_seq(q, t, SEQ_PK);
    for (t = 0; t < NT; t++) q = uk_lock(q, t, LOCK_PK);
    for (t = 0; t < NT; t++) uk_fm(q + t * FM_PK, t);
}

/* ---- the objects: WORK and SLOTS, each in a pair of sectors, the newer copy good */
static uint32_t obj_gen[2], obj_crc[2];         /* the newest copy's save count; the CRC of what it holds */
static const uint8_t *obj_rec(uint32_t s, uint32_t type, uint32_t len, uint32_t ver)
{
    const uint8_t *f = save_flash(s);
    if (!f || st_rd32(f) != OBJ_MAGIC || f[8] != type || f[9] != ver || (f[10] | (uint32_t)f[11] << 8) != len) return 0;
    return st_rd32(f + OBJ_HDR + len) == st_crc(f, OBJ_HDR + len) ? f : 0;
}
static const uint8_t *obj_newest(uint32_t type, uint32_t len, uint32_t ver, uint32_t *s_out)
{
    uint32_t s0 = 1u + type * 2u;
    const uint8_t *a = obj_rec(s0, type, len, ver), *b = obj_rec(s0 + 1u, type, len, ver);
    if (a && b) { if (st_rd32(b + 4) > st_rd32(a + 4)) a = 0; else b = 0; }
    if (s_out) *s_out = b ? s0 + 1u : s0;
    return a ? a : b;
}
static const uint8_t *obj_find(uint32_t type, const uint16_t **vs, uint32_t *s_out)   /* the newest version's newest
                                                 * copy (its row of OBJ_VERS: vs; SLOTS: version 5 and before only) */
{
    uint32_t v;
    for (v = type == OBJ_SLOTS ? 2u : 0u; v < NOBJV; v++) {
        const uint16_t *r = OBJ_VERS[v];
        uint32_t len = type == OBJ_WORK ? WORK_LEN(r[1], r[2], r[3], r[4], r[5]) : 4u * SLOT_LEN(r[1], r[3], r[4]);
        const uint8_t *f = obj_newest(type, len, r[0], s_out);
        if (f) { *vs = r; return f; }
    }
    return 0;
}
/* the payload in st_img (from OBJ_HDR, len bytes) saved if it is not what is saved: into the older copy's sector */
static int obj_save(uint32_t type, uint32_t len)
{
    uint32_t s, k, c = st_crc(st_img + OBJ_HDR, len), s0 = 1u + type * 2u;
    const uint16_t *vs;
    if (obj_gen[type] && c == obj_crc[type]) return 0;   /* (nothing changed) */
    s = s0;
    if (obj_find(type, &vs, &s))                /* the other sector of the pair than the newest copy's (an older */
        s = s == s0 ? s0 + 1u : s0;             /* version's: kept till then) */
    st_wr32(st_img, OBJ_MAGIC), st_wr32(st_img + 4, obj_gen[type] + 1u);
    st_img[8] = (uint8_t)type, st_img[9] = OBJ_VER, st_img[10] = (uint8_t)len, st_img[11] = (uint8_t)(len >> 8);
    st_wr32(st_img + OBJ_HDR + len, st_crc(st_img, OBJ_HDR + len));
    for (k = OBJ_HDR + len + 4u; k < 4096u; k++) st_img[k] = 0xFFu;
    if (st_write(s, st_img)) return -1;
    if (!obj_rec(s, type, len, OBJ_VER)) return -2;   /* (read back: as written) */
    obj_gen[type]++;
    obj_crc[type] = c;
    return 0;
}
/* ---- the slots' stream: slot k at bytes k x SLOT_PK on, its FM patches at 4 x SLOT_PK + k x FM_ALL on; sector i (SAVE
 * sector 3 + i) its bytes i x SLS_P on; each sector: a header (SLS_MAGIC, a count, its index, the version, SLS_P), its
 * bytes, its CRC */
static uint32_t sls_gen;
static uint8_t *sls_byte(uint32_t b)            /* the slot byte at stream byte b (0: none, zeros) */
{
    if (b < 4u * SLOT_PK) return &slots[b / SLOT_PK][b % SLOT_PK];
    b -= 4u * SLOT_PK;
    return b < 4u * FM_ALL ? &slots[b / FM_ALL][SLOT_PK + b % FM_ALL] : 0;
}
static uint32_t sls_fm(uint32_t k) { return (4u * SLOT_PK + k * FM_ALL) / SLS_P; }   /* slot k's FM patches' sector */
static const uint8_t *sls_ok(uint32_t i)        /* sector i, good (its CRC), or 0 */
{
    const uint8_t *f = save_flash(3u + i);
    if (!f || st_rd32(f) != SLS_MAGIC || f[8] != i || f[9] != SLS_VER || (f[10] | (uint32_t)f[11] << 8) != SLS_P) return 0;
    return st_rd32(f + OBJ_HDR + SLS_P) == st_crc(f, OBJ_HDR + SLS_P) ? f : 0;
}
static int sls_blank(uint32_t i)                /* sector i erased (never written: no slot lost there) */
{
    const uint8_t *f = save_flash(3u + i);
    uint32_t k;
    if (!f) return 1;
    for (k = 0; k < 4096u; k++)
        if (f[k] != 0xFFu) return 0;
    return 1;
}
static uint32_t sls_of(uint32_t k)              /* slot k's sectors (bit i; not its FM patches': sls_fm) */
{
    uint32_t a = k * SLOT_PK / SLS_P, b = ((k + 1u) * SLOT_PK - 1u) / SLS_P;
    return (1u << a) | (1u << b);
}
static int sls_write(uint32_t i)                /* sector i as the slots are now, read back: 0 ok */
{
    uint32_t k;
    st_wr32(st_img, SLS_MAGIC), st_wr32(st_img + 4, ++sls_gen);
    st_img[8] = (uint8_t)i, st_img[9] = SLS_VER, st_img[10] = (uint8_t)SLS_P, st_img[11] = (uint8_t)(SLS_P >> 8);
    for (k = 0; k < SLS_P; k++) {
        const uint8_t *s = sls_byte(i * SLS_P + k);
        st_img[OBJ_HDR + k] = s ? *s : 0u;
    }
    st_wr32(st_img + OBJ_HDR + SLS_P, st_crc(st_img, OBJ_HDR + SLS_P));
    if (st_write(3u + i, st_img)) return -1;
    return sls_ok(i) ? 0 : -2;                  /* (read back: as written) */
}
static int slots_save(void)                     /* the slots changed, their sectors (an upgrade: sector 5 first, then
                                                 * the old copies' older, then their newer): 0 ok */
{
    static const uint8_t ORD[2][SLS_N] = {{2, 1, 0}, {2, 0, 1}};   /* (the old newest in sector 3 + 0, or + 1) */
    uint32_t m = 0, k, n;
    for (k = 0; k < 4u; k++)
        if (slot_dirty >> k & 1u) m |= sls_of(k) | 1u << sls_fm(k);
    for (n = 0; n < SLS_N; n++) {
        uint32_t i = slot_up ? ORD[slot_up == 3u ? 0 : 1][n] : n;
        if ((m >> i & 1u) && sls_write(i)) return -1;
    }
    slot_dirty = 0, slot_up = 0;
    return 0;
}
static int autosave(void)                       /* the working project and the slots, each if it changed: 0 ok */
{
    int rc;
    pk_work(st_img + OBJ_HDR);
    if ((rc = obj_save(OBJ_WORK, WORK_PK)) != 0) return rc;
    return slot_dirty ? slots_save() : 0;
}
static int autosave_due(void)                   /* the working project or the slots changed since saved (no flash) */
{
    pk_work(st_img + OBJ_HDR);
    return !obj_gen[OBJ_WORK] || st_crc(st_img + OBJ_HDR, WORK_PK) != obj_crc[OBJ_WORK] || slot_dirty;
}
static void slot_from(uint8_t *d, const uint8_t *q, uint32_t tn)   /* a slot saved before version 6 (its tracks tn B,
                                                 * steps of 3 B: what it has not as at power-on, no chords, no locks) */
{
    uint32_t t, k;
    for (k = 0; k < SLOT_ALL; k++) d[k] = 0;    /* (no FM patches: the PATCHes as built) */
    for (k = 0; k < 4u; k++) d[k] = q[k];
    d += 4, q += 4;
    for (t = 0; t < NT; t++, d += TRK_PK, q += tn) {
        for (k = 0; k < tn; k++) d[k] = q[k];
        trk_def(d, tn);
    }
    for (t = 0; t < NT; t++, d += SEQ_PK, q += SEQ_PK5) {
        for (k = 0; k < 4u; k++) d[k] = q[k];
        for (k = 0; k < SEQ_N; k++)
            d[4 + k * 4u] = q[4 + k * 3u], d[5 + k * 4u] = q[5 + k * 3u], d[6 + k * 4u] = q[6 + k * 3u], d[7 + k * 4u] = 0;
    }
    for (k = 0; k < NT * LOCK_PK; k++) d[k] = 0;
}
static void store_boot(void)                    /* power-on: the newest good copy of the working project, the slots
                                                 * (an older version's: the next autosave writes them as OBJ_VER); the
                                                 * sounds made 64 B ones, if need be */
{
    uint32_t k, i, good = 0, bad = 0, s_old = 0;
    const uint16_t *vs;
    const uint8_t *w, *o;
    snd_upgrade();
    for (k = 0; k < NT; k++) fm_builtin(k, trk[k].p[ENG_FM][0]);   /* (nothing saved: the PATCHes as built) */
    w = obj_find(OBJ_WORK, &vs, 0);
    if (w) {
        uk_work(w + OBJ_HDR, UW_GLOBALS | UW_CHAIN, vs);
        obj_gen[OBJ_WORK] = st_rd32(w + 4);
        obj_crc[OBJ_WORK] = vs[0] == OBJ_VER ? st_crc(w + OBJ_HDR, WORK_PK) : 0;   /* (0: saved again) */
    }
    slot_dirty = slot_lost = slot_up = 0;
    for (i = 0; i < SLS_N; i++) {               /* the stream's sectors: good, or bad (not erased, not good) */
        const uint8_t *f = sls_ok(i);
        if (f) {
            good |= 1u << i;
            if (st_rd32(f + 4) > sls_gen) sls_gen = st_rd32(f + 4);
        } else if (!sls_blank(i))
            bad |= 1u << i;
    }
    if (good != (1u << SLS_N) - 1u && (o = obj_find(OBJ_SLOTS, &vs, &s_old)) != 0) {   /* saved before version 6: */
        for (k = 0; k < 4u; k++)                /* (or a cut during the upgrade: again from the old copy left) */
            slot_from(slots[k], o + OBJ_HDR + k * SLOT_LEN(vs[1], vs[3], vs[4]), vs[1]);
        slot_dirty = 15u, slot_up = s_old;      /* (written as the stream at the next autosave) */
        return;
    }
    for (k = 0; k < 4u; k++) {                  /* each slot from its sectors; one of them bad: lost, empty */
        uint32_t m = sls_of(k), b;
        slots[k][0] = 0;
        if (m & bad) {
            slot_lost |= 1u << k, slot_dirty |= 1u << k;   /* (written again, empty: the next power-on clean) */
            continue;
        }
        if ((m & good) != m) continue;          /* (never written: empty) */
        for (b = k * SLOT_PK; b < (k + 1u) * SLOT_PK; b++)
            slots[k][b - k * SLOT_PK] = save_flash(3u + b / SLS_P)[OBJ_HDR + b % SLS_P];
        for (b = 0; b < FM_ALL; b++) {           /* its FM patches (their sector not good: none, the PATCHes as built) */
            uint32_t sb = 4u * SLOT_PK + k * FM_ALL + b;
            slots[k][SLOT_PK + b] = good >> sls_fm(k) & 1u ? save_flash(3u + sb / SLS_P)[OBJ_HDR + sb % SLS_P] : 0u;
        }
    }
}

/* ---- the slots A..D */
static void slot_save(uint32_t s) { pk_slot(slots[s & 3u]); slot_cur = (int32_t)(s & 3u); slot_dirty |= 1u << (s & 3u); }
static void slot_erase(uint32_t s) { slots[s & 3u][0] = 0; slot_dirty |= 1u << (s & 3u); }
static int slot_used(uint32_t s) { return slots[s & 3u][0] == 1u; }
static int slot_has(uint32_t s, uint32_t t)     /* slot s used, track t's sequence in it with a note (SONG's lanes) */
{
    const uint8_t *q = slots[s & 3u] + 4u + NT * TRK_PK + (t % NT) * SEQ_PK;
    uint32_t k, n = (uint32_t)clamp(q[0], 1, SEQ_N);
    if (!slot_used(s)) return 0;
    for (k = 0; k < n; k++)
        if (q[4u + k * 4u] & 127u) return 1;
    return 0;
}
static void slot_recall(uint32_t s)             /* now (stopped) */
{
    if (!slot_used(s)) return;
    uk_slot(slots[s & 3u], 0);
    slot_cur = (int32_t)(s & 3u);
}
static void slot_apply_isr(uint32_t s)          /* the audio ISR, at the bar (seq.c): the slot in, its sequences from step 1 */
{
    uk_slot(slots[s & 3u], 1);
    slot_cur = (int32_t)(s & 3u);
}
static void project_new(void)                   /* NEW: the four tracks' power-on sounds, empty sequences */
{
    static const track_t T0 = TRK_DEF;
    uint32_t t, e, k;
    for (t = 0; t < NT; t++) {
        for (e = 0; e < NENG; e++)
            for (k = 0; k < 4u; k++) trk[t].p[e][k] = T0.p[e][k];
        for (k = 0; k < 4u; k++) trk[t].env[k] = T0.env[k];
        if (trk[t].latch) nq_put(NQ_RELEASE, t, 0);
        trk[t].latch = T0.latch;
        trk[t].chord = T0.chord, trk[t].ch_type = T0.ch_type, trk[t].ch_voice = T0.ch_voice;
        trk[t].arp = T0.arp, trk[t].arp_mode = T0.arp_mode, trk[t].arp_rate = T0.arp_rate;
        trk[t].arp_oct = T0.arp_oct, trk[t].arp_gate = T0.arp_gate;
        trk[t].lfo_rate = T0.lfo_rate, trk[t].lfo_depth = T0.lfo_depth, trk[t].lfo_dest = T0.lfo_dest;
        trk[t].lfo_shape = T0.lfo_shape;
        fm_builtin(t, T0.p[ENG_FM][0]);
        if (trk[t].eng != T0.eng) {
            trk[t].eng = T0.eng;
            nq_put(NQ_RESTART, t, 0);
        }
    }
    seq_clear();
}

/* ---- undo / redo: one level. A snapshot: the tracks and sequences (WORK_PK, the effects and tempo not restored), and
 * a slot if the action changed one */
#define UNDO_SZ (WORK_PK + 1u + SLOT_ALL)
static uint8_t undo_b[UNDO_SZ] __attribute__((section(".pool"))), redo_b[UNDO_SZ];   /* (one in the pool: RAM is
                                                 * tight) */
static uint32_t undo_ok, redo_ok, undo_key;
static uint32_t undo_chain;                     /* the snapshot is a song record's (grimm.c sets it after undo_take):
                                                 * undo, redo bring the chain back too */
static void snap(uint8_t *b, int32_t slot)      /* (slot -1: none) */
{
    uint32_t i;
    pk_work(b);
    b[WORK_PK] = (uint8_t)(slot < 0 ? 0xFFu : (uint32_t)slot);
    if (slot >= 0)
        for (i = 0; i < SLOT_ALL; i++) b[WORK_PK + 1u + i] = slots[slot][i];
}
static void unsnap(const uint8_t *b)
{
    uint32_t i, s = b[WORK_PK];
    uk_work(b, undo_chain ? UW_CHAIN : 0u, OBJ_VERS[0]);   /* (the song's chain: a song record's undo only) */
    if (s < 4u) {
        for (i = 0; i < SLOT_ALL; i++) slots[s][i] = b[WORK_PK + 1u + i];
        slot_dirty |= 1u << s;
    }
}
/* before an undoable action: the state now kept (an action of the same key as the last: kept from the first) */
static void undo_take(uint32_t key, int32_t slot)
{
    if (key && key == undo_key && undo_ok) return;
    snap(undo_b, slot);
    undo_ok = 1, redo_ok = 0, undo_key = key, undo_chain = 0;
}
static int undo(void)
{
    if (!undo_ok) return -1;
    snap(redo_b, undo_b[WORK_PK] < 4u ? undo_b[WORK_PK] : -1);
    unsnap(undo_b);
    undo_ok = 0, redo_ok = 1, undo_key = 0;
    return 0;
}
static int redo(void)
{
    if (!redo_ok) return -1;
    snap(undo_b, redo_b[WORK_PK] < 4u ? redo_b[WORK_PK] : -1);
    unsnap(redo_b);
    redo_ok = 0, undo_ok = 1, undo_key = 0;
    return 0;
}
