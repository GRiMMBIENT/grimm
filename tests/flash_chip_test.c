/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* The two flash chips (tests/run_host.sh): the app's real storage code (firmware/src/flash_store.c: the settings, the samples, the saves, and flash_probe, the id check) on a simulated 1 MiB NOR chip
 * that reports a JEDEC id: 0xB36014 (UC25HQ80) saves, and loads after a power cycle, and the updater is allowed (the gate platform.c and recovery.c use: flash_ok); 0x856014 (Puya P25Q80) the same,
 * unchanged; an unknown id is refused (nothing written, nothing read, no update). The update loader's gate is the same macro, FL_JEDEC_OK (firmware/hal/fm1_flash_id.h, checked here too).
 * The loader's whole update flow runs only on the FM-1. */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define FL_FAR(fn) (&(fn))
static uint32_t sim_id;
static uint8_t sim_mem[1u << 20];
static uint32_t fl_jedec_ram(void) { return sim_id; }
static uint32_t irq_save(void) { return 0; }
static void irq_restore(uint32_t f) { (void)f; }
static int fl_read_ram(uint32_t off, uint8_t *dst, uint32_t n) { memcpy(dst, sim_mem + off, n); return 0; }
static void fm1_wdt_feed(void) {}
static const uint8_t *fm1_xip_ptr(uint32_t off) { return sim_mem + off; }
static int fl_erase4k(uint32_t off, uint32_t *took)             /* a 4 KiB sector to 0xFF (NOR) */
{
    if ((off & 0xFFFu) || off >= sizeof sim_mem) return -1;
    memset(sim_mem + off, 0xFF, 4096);
    *took = 50000;
    return 0;
}
static int fl_write(uint32_t off, const uint8_t *src, uint32_t n)   /* a program can only clear bits: into erased flash */
{
    uint32_t i;
    if (off + n > sizeof sim_mem) return -1;
    for (i = 0; i < n; i++) sim_mem[off + i] &= src[i];
    return 0;
}

#include "../firmware/hal/fm1_flash_id.h"
#include "../firmware/src/flash_map.h"
static uint8_t flash_ok;
#include "../firmware/src/flash_store.c"

static int bad;
static void check(const char *what, int ok)
{
    printf("  %-4s  flash chip: %s\n", ok ? "ok" : "FAIL", what);
    bad += !ok;
}
static void power_cycle(void)                   /* RAM gone, the flash kept: the id read again at power-on */
{
    flash_ok = 0;
    flash_probe();
}
static int erased(void)
{
    uint32_t i;
    for (i = 0; i < sizeof sim_mem; i++)
        if (sim_mem[i] != 0xFF) return 0;
    return 1;
}
static int chip(uint32_t id, int known)
{
    uint8_t set[40], got[40], sv[100], *rd;
    uint32_t i, ok = 1;
    char what[160];
    memset(sim_mem, 0xFF, sizeof sim_mem);
    sim_id = id;
    for (i = 0; i < sizeof set; i++) set[i] = (uint8_t)(i * 7 + 3);
    for (i = 0; i < sizeof sv; i++) sv[i] = (uint8_t)(0xA0 ^ i);
    power_cycle();
    snprintf(what, sizeof what, "id 0x%06X: the chip is %s (flash_ok %u: the saves, the samples and the updater %s)", id, known ? "known" : "refused", flash_ok, known ? "allowed" : "not");
    check(what, flash_ok == (known ? 1u : 0u));
    ok &= settings_save(set, sizeof set) == (known ? 0 : -1);
    ok &= save_flash_erase(2) == (known ? 0 : -8);
    ok &= save_flash_write(2, 0, sv, sizeof sv) == (known ? 0 : -8);
    ok &= sample_flash_erase(0x1000) == (known ? 0 : -8);
    ok &= sample_flash_write(0x1000, sv, sizeof sv) == (known ? 0 : -8);
    snprintf(what, sizeof what, "id 0x%06X: settings, a save sector and a sample sector %s", id, known ? "written" : "refused (-1, -8: nothing written)");
    check(what, ok);
    power_cycle();                              /* power off and on */
    memset(got, 0, sizeof got);
    if (known) {
        rd = (uint8_t *)save_flash(2);
        snprintf(what, sizeof what, "id 0x%06X: after a power cycle the settings load, the save and the sample read back as written", id);
        check(what, settings_load(got, sizeof got) == 0 && !memcmp(got, set, sizeof got) && rd && !memcmp(rd, sv, sizeof sv) && sample_flash() && !memcmp(sample_flash() + 0x1000, sv, sizeof sv));
    } else {
        snprintf(what, sizeof what, "id 0x%06X: after a power cycle nothing loads (the saves were RAM's), no flash pointer, the chip untouched (all 0xFF)", id);
        check(what, settings_load(got, sizeof got) != 0 && !save_flash(0) && !sample_flash() && erased());
    }
    return ok;
}

int main(void)
{
    check("FL_JEDEC_OK: 0x856014 (Puya P25Q80) and 0xB36014 (UC25HQ80) yes; 0, 0xFFFFFF, 0x856015, 0xB36013, 0x146014, 0xEF4014 (Winbond) no",
          FL_JEDEC_OK(0x856014u) && FL_JEDEC_OK(0xB36014u) && !FL_JEDEC_OK(0u) && !FL_JEDEC_OK(0xFFFFFFu) && !FL_JEDEC_OK(0x856015u) && !FL_JEDEC_OK(0xB36013u) && !FL_JEDEC_OK(0x146014u) &&
          !FL_JEDEC_OK(0xEF4014u));
    chip(0x856014u, 1);
    chip(0xB36014u, 1);
    chip(0x000000u, 0);
    chip(0xFFFFFFu, 0);
    chip(0xEF4014u, 0);
    return bad ? 1 : 0;
}
