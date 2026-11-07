/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 Leo Kuroshita (@kurogedelic), Hügelton Instruments; GR!MM changes (the sample and save sectors; the chip's id by FL_JEDEC_OK) Copyright (C) 2026 GR!MM */
/* The app's storage in the flash (included by platform.c; tests/flash_chip_test.c runs it on a simulated chip): read, the settings, the sample area, the saves. flash_ok: the chip is one this firmware
 * knows (FL_JEDEC_OK: both the Puya P25Q80 and the UC25HQ80); without it nothing is written or read from the flash (the saves stay in RAM, store.c; the updater is not started). */

static int st_read(uint32_t off, void *dst, uint32_t n)
{
    uint8_t *d = dst;
    while (n) {
        uint32_t k = n > 256u ? 256u : n, f = irq_save();
        int rc = FL_FAR(fl_read_ram)(off, d, k);
        irq_restore(f);
        if (rc)
            return rc;
        off += k;
        d += k;
        n -= k;
    }
    return 0;
}

static void flash_probe(void)                    /* at power-on, interrupts off: is this a chip we know */
{
    flash_ok = FL_JEDEC_OK(FL_FAR(fl_jedec_ram)()) ? 1 : 0;
}
/* ---- settings: [magic][length][crc32] then the bytes; settings_save erases the sector (~50 ms with
 * the interrupts off: the sound drops out), so save when nothing plays, not on every knob step */
#define SETTINGS_MAGIC 0x59474E44u              /* "DNGY" */
static uint32_t crc32(const uint8_t *p, uint32_t n)
{
    uint32_t c = 0xFFFFFFFFu, k;
    while (n--) {
        c ^= *p++;
        for (k = 0; k < 8u; k++)
            c = c & 1u ? (c >> 1) ^ 0xEDB88320u : c >> 1;
    }
    return ~c;
}
static int settings_load(void *p, uint32_t n)   /* 0: loaded; else p is untouched */
{
    static uint8_t buf[256];
    uint32_t h[3];
    if (!flash_ok || n > sizeof buf || st_read(SETTINGS_AT, h, sizeof h) || h[0] != SETTINGS_MAGIC || h[1] != n)
        return -1;
    if (st_read(SETTINGS_AT + sizeof h, buf, n) || crc32(buf, n) != h[2])
        return -1;
    memcpy(p, buf, n);
    return 0;
}
static int settings_save(const void *p, uint32_t n)   /* p in RAM, n <= 256 */
{
    uint32_t h[3] = {SETTINGS_MAGIC, n, 0}, took;
    int rc;
    if (!flash_ok || n > 256u)
        return -1;
    h[2] = crc32(p, n);
    if ((rc = fl_erase4k(SETTINGS_AT, &took)) != 0)
        return rc;
    if ((rc = fl_write(SETTINGS_AT + sizeof h, p, n)) != 0)
        return rc;
    return fl_write(SETTINGS_AT, (const uint8_t *)h, sizeof h);   /* the header last: a cut save stays invalid */
}

/* ---- GR!MM: the sample area (samples.c) and SysEx for the app. An erase or a write stops everything (the
 * interrupts are off, ~50 ms an erase): only while nothing sounds */
static const uint8_t *sample_flash(void) { return flash_ok ? fm1_xip_ptr(SAMPLE_AREA) : 0; }   /* 0: no flash */
static int sample_flash_erase(uint32_t off)     /* the 4 KiB sector at off (in the area) */
{
    uint32_t took;
    if (!flash_ok || (off & 0xFFFu) || off >= SAMPLE_AREA_LEN)
        return -8;
    fm1_wdt_feed();                             /* (a slot is 18 erases, RESTORE 72: the watchdog fed between) */
    return fl_erase4k(SAMPLE_AREA + off, &took);
}
static int sample_flash_write(uint32_t off, const void *p, uint32_t n)   /* p in RAM; into erased flash */
{
    if (!flash_ok || off >= SAMPLE_AREA_LEN || n > SAMPLE_AREA_LEN - off)
        return -8;
    return fl_write(SAMPLE_AREA + off, (const uint8_t *)p, n);
}
/* GR!MM's saves (store.c): sector i of SAVE_SECT, read through XIP; an erase (~50 ms) or a write stops the audio a moment */
static const uint8_t *save_flash(uint32_t i) { return flash_ok && i < SAVE_NSECT ? fm1_xip_ptr(SAVE_SECT[i]) : 0; }
static int save_flash_erase(uint32_t i)
{
    uint32_t took;
    if (!flash_ok || i >= SAVE_NSECT) return -8;
    return fl_erase4k(SAVE_SECT[i], &took);
}
static int save_flash_write(uint32_t i, uint32_t off, const void *p, uint32_t n)   /* p in RAM; into erased flash */
{
    if (!flash_ok || i >= SAVE_NSECT || off >= 4096u || n > 4096u - off) return -8;
    return fl_write(SAVE_SECT[i] + off, (const uint8_t *)p, n);
}
