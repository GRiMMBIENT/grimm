/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM (the ghost test; the layout: fm1_input.h's) */
/* The FM-1's switch matrix: where each key, button and encoder contact sits (fm1_input.h scans it), and the ghost
 * test. The matrix has no diode a switch: while closed switches join a row to a column through others (a rectangle:
 * three corners closed, or a longer chain), the switch at that row and column reads closed too, pressed or not.
 * fm1_ghost says if a switch can read closed that way; the input layer then takes a new press there as a ghost (one
 * pressed before the others stays). Pure logic, no registers: tests/host.c simulates the matrix with it (the stress
 * test). */
#pragma once
#include <stdint.h>

#define FM1_NCOL 11u
#define FM1_NKEY 41u              /* ids: 0..13 buttons, 14..40 note keys */
#define FM1_NENC 7u

/* key id at (physical column, packed row bit), -1 = none */
static const int8_t FM1_KEYMAP[6][FM1_NCOL] = {
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PA0: encoders */
    { 5, 11,  4, 10,  3,  9,  2,  8, -1, -1, -1},          /* PA5 */
    {34, 35, 36, 37, 38, 40, 39, 13,  7,  6, 12},          /* PA6 */
    {23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33},          /* PA7 */
    { 0,  1, 15, 14, 17, 16, 19, 18, 20, 21, 22},          /* PA8 */
    {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1},          /* PB7: encoder 6 */
};
/* encoder i: A at (col, row bit), B at (col, row bit) */
static const uint8_t FM1_ENC[FM1_NENC][4] = {
    {0, 0, 1, 0}, {2, 0, 3, 0}, {8, 1, 9, 1}, {8, 0, 9, 0}, {6, 0, 7, 0}, {4, 0, 5, 0}, {0, 5, 1, 5},
};

static uint32_t fm1_enc_rows(uint32_t c)        /* column c's encoder contacts (row bits) */
{
    uint32_t e, m = 0;
    for (e = 0; e < FM1_NENC; e++) {
        if (FM1_ENC[e][0] == c) m |= 1u << FM1_ENC[e][1];
        if (FM1_ENC[e][2] == c) m |= 1u << FM1_ENC[e][3];
    }
    return m;
}
/* closed[c]: the row bits of the switches closed in column c. Switch (r, p): are its row and its column joined through
 * the others (closed[] without it)? Then it can read closed while open: a ghost */
static int fm1_ghost(const uint8_t *closed, uint32_t r, uint32_t p)
{
    uint32_t rows = 1u << r, cols = 0, more = 1, c;
    while (more) {
        more = 0;
        for (c = 0; c < FM1_NCOL; c++) {
            uint32_t m = closed[c] & (c == p ? ~(1u << r) : 0x3Fu) & 0x3Fu;
            if (!(m & rows)) continue;          /* (not reached from the rows joined so far) */
            if (!(cols >> c & 1u)) cols |= 1u << c, more = 1;
            if (m & ~rows) rows |= m, more = 1;
        }
        if (cols >> p & 1u) return 1;
    }
    return 0;
}
