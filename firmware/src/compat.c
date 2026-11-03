/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* Saves across versions (host mode, tests/run_host.sh): a project as an older GR!MM saved it loads as it did there.
 * Environment, at the end of a host run (firmware/src/host.c):
 *   GRIMM_FILL=1          a varied project into RAM (every step's values over their ranges, chord steps, locks, the
 *                         patterns' settings), into slots A..C, and saved (autosave), with two user sounds
 *   GRIMM_SAVE_OUT=file   the save sectors (6 x 4 KiB) into file
 *   GRIMM_STATE_OUT=file  what was loaded, as text: each step's values, the packed project, the slots, the settings
 *                         (all but their version, which a newer GR!MM raises; then the bytes it adds) and the sounds
 *                         (hex)
 * and at the start, GRIMM_SAVE_IN=file: the save sectors from file (firmware/src/host.c main).
 * tests/data/v1.0-saves.bin was made by GR!MM 1.0 (the v1.0 tag) with this file and GRIMM_FILL; v1.0-state.txt is what
 * 1.0 loaded from it. The same source works on both: the steps' fields are assigned, never pointed to. */
static uint32_t cp_rng = 0x2468ACE1u;
static uint32_t cp_r(uint32_t n)
{
    cp_rng ^= cp_rng << 13, cp_rng ^= cp_rng >> 17, cp_rng ^= cp_rng << 5;
    return n ? cp_rng % n : 0u;
}
static void compat_fill_seq(void)               /* every track's pattern: a varied one */
{
    uint32_t t, k, j;
    for (t = 0; t < NT; t++) {
        seq[t].steps = 1 + (int32_t)cp_r(SEQ_N), seq[t].div = (int32_t)cp_r(8), seq[t].swing = (int32_t)cp_r(128);
        seq[t].mode = (int32_t)cp_r(SEQ_NMODE);
        for (k = 0; k < SEQ_N; k++) {
            seq_step_t *s = &seq[t].pat[0].st[k];
            if (!cp_r(4)) {                     /* (an empty step) */
                s->note = 0, s->chance = 100, s->ratchet = 1, s->len = 1, s->chord = 0, s->lmask = 0;
                continue;
            }
            s->note = 1 + cp_r(127), s->chance = cp_r(101), s->ratchet = 1 + cp_r(4), s->len = 1 + cp_r(SEQ_N);
            s->chord = cp_r(3) ? 0 : 0x80u | cp_r(NVOICING) << 4 | cp_r(NCHORD);
            s->lmask = cp_r(16);
            for (j = 0; j < 4u; j++) s->lv[j] = (uint8_t)cp_r(256);
        }
    }
}
static void compat_hex(FILE *f, const char *what, const uint8_t *p, uint32_t n)
{
    uint32_t i;
    fprintf(f, "%s %u\n", what, n);
    for (i = 0; i < n; i++) fprintf(f, "%02x%s", p[i], (i & 31u) == 31u || i + 1u == n ? "\n" : "");
}
/* (V2.0.2, tests/samplefix_test.py) the sample area as another firmware or a cut upload left it, before boot:
 *   GRIMM_SMP_PRE=xxxx    a letter a slot: - blank, J junk bytes, P data without a header (first sector erased), C a good
 *                         header and data with one byte wrong (the CRC fails), U a good sample of the user's (named
 *                         USERTEST), F the factory sample
 *   GRIMM_SMP_FAIL=1      every erase fails (a write that cannot be done: it is not tried again)
 *   GRIMM_SMP_OUT=file    at the end, a line a slot: what it holds (playing, name, as the factory's, as before boot) and
 *                         the erases tried */
static uint32_t cp_smp_pre[SMP_SLOTS];
static uint32_t cp_smp_fnv(const uint8_t *p, uint32_t n)
{
    uint32_t h = 2166136261u;
    while (n--) h = (h ^ *p++) * 16777619u;
    return h;
}
static void compat_init(void)                   /* firmware/src/host.c: before app_main */
{
    const char *e = getenv("GRIMM_SMP_PRE");
    uint32_t k, i, r = 0x9E3779B9u;
    host_erase_fail = getenv("GRIMM_SMP_FAIL") != 0;
    for (k = 0; e && *e && k < SMP_SLOTS; k++, e++) {
        uint8_t *s = fake_flash + k * SMP_SLOT_SIZE;
        if (*e == 'J')
            for (i = 0; i < SMP_SLOT_SIZE; i++) r = r * 1664525u + 1013904223u, s[i] = (uint8_t)(r >> 24);
        else if (*e == 'P' || *e == 'C' || *e == 'U' || *e == 'F') {
            memcpy(s + SMP_DATA_OFF, SMP_FACT_DATA[k], SMP_FACT_LEN[k]);
            if (*e == 'P') memset(s, 0xFF, 0x1000);
            else memcpy(s, SMP_FACT_HDR[k], SMP_HDR_LEN);
            if (*e == 'C') s[SMP_DATA_OFF + 100] ^= 0x55;
            if (*e == 'U') memcpy(s + 8, "USERTEST", 8);
        }
        cp_smp_pre[k] = cp_smp_fnv(s, SMP_SLOT_SIZE);
    }
    for (; k < SMP_SLOTS; k++) cp_smp_pre[k] = cp_smp_fnv(fake_flash + k * SMP_SLOT_SIZE, SMP_SLOT_SIZE);
}
static void compat_exit(void)                   /* firmware/src/host.c: at the end of the run */
{
    const char *e;
    FILE *f;
    uint32_t t, k;
    if ((e = getenv("GRIMM_SMP_OUT")) != 0 && (f = fopen(e, "w")) != 0) {
        for (k = 0; k < SMP_SLOTS; k++) {
            const uint8_t *s = fake_flash + k * SMP_SLOT_SIZE;
            fprintf(f, "slot %u playing %u name %s factory %u same %u\n", k + 1u, smp[k].ready, smp[k].ready ? smp[k].name : "-",
                    !memcmp(s, SMP_FACT_HDR[k], SMP_HDR_LEN) && !memcmp(s + SMP_DATA_OFF, SMP_FACT_DATA[k], SMP_FACT_LEN[k]),
                    cp_smp_fnv(s, SMP_SLOT_SIZE) == cp_smp_pre[k]);
        }
        fprintf(f, "erases %u\n", host_erases);
        fclose(f);
    }
    if (getenv("GRIMM_FILL")) {
        for (k = 0; k < 3u; k++) compat_fill_seq(), slot_save(k);
        compat_fill_seq();
        snd_save(4, 0, 1), snd_save(9, 1, 6);
        autosave();
    }
    if ((e = getenv("GRIMM_SAVE_OUT")) != 0 && (f = fopen(e, "wb")) != 0)
        fwrite(fake_save, 1, sizeof fake_save, f), fclose(f);
    if ((e = getenv("GRIMM_STATE_OUT")) != 0 && (f = fopen(e, "w")) != 0) {
        for (t = 0; t < NT; t++) {
            fprintf(f, "track %u steps %d div %d swing %d mode %d\n", t, (int)seq[t].steps, (int)seq[t].div,
                    (int)seq[t].swing, (int)seq[t].mode);
            for (k = 0; k < SEQ_N; k++) {
                const seq_step_t *s = &seq[t].pat[0].st[k];
                fprintf(f, "%2u %3d %3d %d %2d %3d %2d %3d %3d %3d %3d\n", k, (int)s->note, (int)s->chance,
                        (int)s->ratchet, (int)s->len, (int)s->chord, (int)s->lmask, s->lv[0], s->lv[1], s->lv[2], s->lv[3]);
            }
        }
        pk_work(st_img);
#ifdef FX2_PK
        compat_hex(f, "work", st_img, WORK_PK - FX2_PK);   /* (V1.0's part: V2's MASTER and DRIVE after it, not 1.0's) */
#else
        compat_hex(f, "work", st_img, WORK_PK);
#endif
        for (k = 0; k < 4u; k++) compat_hex(f, "slot", slots[k], SLOT_ALL);
        compat_hex(f, "settings", (const uint8_t *)&set, 167u);   /* (V1.0's and later's alike: all but the version,
                                                 * then what comes after it: V2's CLOCK, 0 INT; 1.0: none, 0) */
        fprintf(f, "after %u\n", sizeof set > 168u ? (unsigned)((const uint8_t *)&set)[168] : 0u);
        compat_hex(f, "sounds", save_flash(0), 4096u);
        fclose(f);
    }
}
