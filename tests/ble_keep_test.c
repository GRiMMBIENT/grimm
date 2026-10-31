/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* firmware/src/ble_keep.c against a fake NOR flash (a write only clears bits, an erase sets a sector to FF), the result
 * read back by the BLE stack's own reader (src/ble/ble_vm.c ble_vm_scan), as on the FM-1.
 * tests/run_host.sh runs it. */
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define BLE_API static
#include "../firmware/src/ble/ble_vm.c"

static uint8_t fl[0x100000];
static uint32_t n_erase, n_write;
static uint8_t flash_ok = 1;
static int st_read(uint32_t off, void *dst, uint32_t n) { memcpy(dst, fl + off, n); return 0; }
static int fl_erase4k(uint32_t off, uint32_t *took)
{
    (void)took;
    assert(off == 0xE8000u);                    /* (the only sector it may erase) */
    memset(fl + off, 0xFF, 0x1000), n_erase++;
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)
{
    uint32_t i;
    assert(off >= 0xE8000u && off + n <= 0xE9000u);   /* (the only sector it may write) */
    for (i = 0; i < n; i++) fl[off + i] &= src[i];
    n_write++;
    return 0;
}
#define BLE_VM_AT 0x000E8000u
#include "../firmware/src/ble_keep.c"

static uint32_t at;                             /* the log being made */
static void rec(uint32_t id, uint8_t fill, uint32_t len)
{
    uint8_t d[200];
    uint32_t c;
    memset(d, fill, len);
    if (len >= 66u && id == 187u) {             /* 187: its own CRC at 64, little-endian */
        c = ble_crc16_xmodem(0, d, 64);
        d[64] = (uint8_t)c, d[65] = (uint8_t)(c >> 8), d[66] = d[67] = 0;
    }
    c = ble_crc16_xmodem(0, d, len);
    fl[at] = (uint8_t)c, fl[at + 1] = (uint8_t)id, fl[at + 2] = (uint8_t)((id >> 8 & 15u) | (len & 15u) << 4);
    fl[at + 3] = (uint8_t)(len >> 4);
    memcpy(fl + at + 4, d, len);
    at += 4 + len;
}
static void unit_c2000(void)                    /* this FM-1's: app records, the trims twice (the last wins), past 4 KiB */
{
    uint32_t i;
    memset(fl, 0xFF, sizeof fl);
    at = 0xC2000u;
    fl[at] = 0x55, fl[at + 1] = 0xAA, fl[at + 2] = 0xAA, fl[at + 3] = 0x55, at += 4;
    for (i = 1; i <= 24; i++) rec(i, (uint8_t)i, 150);
    rec(106, 0x11, 2), rec(107, 0x11, 7), rec(108, 0x11, 20), rec(187, 0x11, 68);   /* older */
    for (i = 25; i <= 40; i++) rec(i, (uint8_t)i, 150);
    assert(at > 0xC3000u);
    rec(106, 0x22, 2), rec(107, 0x22, 7), rec(108, 0x22, 20), rec(187, 0x22, 68);   /* the last */
    rec(41, 41, 30);
}
static int rd(void *ctx, uint32_t off, uint8_t *dst, uint32_t n) { (void)ctx; memcpy(dst, fl + off, n); return 0; }
static int scan_ok(uint8_t want)                /* the stack finds a complete set at 0xE8000, its records filled with want */
{
    struct ble_vm_info info;
    struct ble_rf_trims t;
    if (!ble_vm_scan(rd, 0, &info, &t, 0, 0) || info.area != 0xE8000u)
        return 0;
    return t.x106[0] == want && t.x107[6] == want && t.x108[19] == want && t.x187[0] == want;
}

int main(void)
{
    uint8_t keep[0x1000];
    /* 1. this unit: 0xE8000 holds GR!MM's earlier 4 KiB copy: erased, the trims written; the stack finds the last set */
    unit_c2000();
    memcpy(fl + 0xE8000u, fl + 0xC2000u, 0x1000);
    assert(!scan_ok(0x22));                     /* (the 4 KiB copy alone: incomplete, as NO CAL showed) */
    n_erase = n_write = 0;
    ble_vm_keep();
    assert(n_erase == 1 && n_write == 1 && scan_ok(0x22));
    assert(fl[0xE8000u + 117] == 0xFF);         /* (117 bytes: the mark and the four records, nothing more) */
    /* 2. again: written already, nothing done */
    n_erase = n_write = 0;
    ble_vm_keep();
    assert(n_erase == 0 && n_write == 0 && scan_ok(0x22));
    /* 3. 0xE8000 erased (a cut between the erase and the write): written, no erase */
    unit_c2000();
    n_erase = n_write = 0;
    ble_vm_keep();
    assert(n_erase == 0 && n_write == 1 && scan_ok(0x22));
    /* 4. 0xE8000 holds something else (stock's own VM): left as it is */
    unit_c2000();
    memset(fl + 0xE8000u, 0x5A, 0x1000);
    memcpy(keep, fl + 0xE8000u, sizeof keep);
    n_erase = n_write = 0;
    ble_vm_keep();
    assert(n_erase == 0 && n_write == 0 && !memcmp(keep, fl + 0xE8000u, sizeof keep));
    /* 5. a trim record missing from the log (no 108): nothing written */
    memset(fl, 0xFF, sizeof fl), fl[0xC2000u] = 0x55, fl[0xC2001u] = 0xAA, fl[0xC2002u] = 0xAA, fl[0xC2003u] = 0x55;
    at = 0xC2000u + 4;
    rec(1, 1, 150), rec(106, 0x22, 2), rec(107, 0x22, 7), rec(187, 0x22, 68);
    n_erase = n_write = 0;
    ble_vm_keep();
    assert(n_erase == 0 && n_write == 0);
    /* 6. no VM below 0xE8000: nothing */
    memset(fl, 0xFF, sizeof fl);
    ble_vm_keep();
    assert(n_erase == 0 && n_write == 0);
    puts("ble_keep: ok");
    return 0;
}
