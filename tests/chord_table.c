/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* Prints every chord the firmware's scale.c builds: TYPE 0..10, VOICING 0..4, ROOT 0..127 (SCALE off), a line each:
 * "ty vo root count notes...". web/test_web.mjs checks the editor's JS port (editor_core.js chordNotes, used by STEP RECORD) against
 * the hash of this text (tests/data/chord_table.sha256).
 *   cc -I firmware/src -o build/chord_table tests/chord_table.c && build/chord_table | sha256sum */
#include <stdint.h>
#include <stdio.h>
static int32_t clamp(int32_t v, int32_t lo, int32_t hi) { return v < lo ? lo : v > hi ? hi : v; }
static struct { int32_t ch_type, ch_voice; } trk[4];
static char *put_s(char *s, const char *t) { while (*t) *s++ = *t++; *s = 0; return s; }
#include "scale.c"
int main(void)
{
    int32_t ty, vo, r;
    uint8_t out[8];
    uint32_t n, i;
    (void)trk; (void)put_s;
    for (ty = 0; ty < NCHORD; ty++)
        for (vo = 0; vo < NVOICING; vo++)
            for (r = 0; r < 128; r++) {
                n = chord_notes_tv(ty, vo, (uint32_t)r, out);
                printf("%d %d %d %u", ty, vo, r, n);
                for (i = 0; i < n; i++) printf(" %u", out[i]);
                printf("\n");
            }
    return 0;
}
