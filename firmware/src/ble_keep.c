/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* The radio's trims kept where the BLE stack finds them (platform.c, GRIMM_BLE; tests/ble_keep_test.c).
 *
 * Stock keeps its VM (the radio's trims among its records, made at the factory, one set per unit) where its region
 * allows: one FM-1 at 0xE8000, the stack's first candidate (src/ble/ble_vm.h); another at 0xC2000, its log running
 * ~10 KiB, in GR!MM's sample area, where a sample written over it would lose the trims for good. So: the log of the
 * first sector below 0xE8000 that starts with the VM's mark is read as ble_vm.c reads one (records back to back from
 * +4: [check: low byte of the data's CRC-16/XMODEM] [id 7:0] [id 11:8 | len 3:0 << 4] [len 11:4], the data; it ends at
 * the first failing check; the last record of an id wins), and a VM of its four trim records, each as it is (header
 * and data), is written at 0xE8000: the stack finds it there. 0xE8000 is written only erased, or erased first only
 * when it holds GR!MM's own earlier copy (the first 4 KiB of that log, as they are); anything else there is left.
 *
 * The includer gives: flash_ok, st_read(off, dst, n), fl_erase4k(off, &took), fl_write(off, src, n) (0 = ok),
 * BLE_VM_AT (0xE8000). */
#define BLE_VM_LOG_MAX (64u << 10)              /* a log read 64 KiB at most */
static uint32_t ble_crc16(uint32_t c, const uint8_t *p, uint32_t n)   /* CRC-16/XMODEM (ble_vm.c's) */
{
    uint32_t k;
    while (n--)
        for (c ^= (uint32_t)*p++ << 8, k = 0; k < 8u; k++)
            c = (c & 0x8000u ? (c << 1) ^ 0x1021u : c << 1) & 0xFFFFu;
    return c;
}
static int ble_vm_same(uint32_t a, uint32_t b, uint32_t n, int blank)   /* flash a..a+n as at b, or (blank) all FF */
{
    uint8_t x[64], y[64];
    uint32_t i, k;
    for (i = 0; i < n; i += sizeof x) {
        if (st_read(a + i, x, sizeof x) || (!blank && st_read(b + i, y, sizeof y)))
            return 0;
        for (k = 0; k < sizeof x; k++)
            if (x[k] != (blank ? 0xFFu : y[k]))
                return 0;
    }
    return 1;
}
static void ble_vm_keep(void)
{
    static const uint16_t RID[4] = {106, 107, 108, 187};
    static const uint8_t RLEN[4] = {2, 7, 20, 68};
    uint8_t img[4 + 4 * 4 + 2 + 7 + 20 + 68], d[sizeof img], h[4], c[32];
    uint32_t a, off = 4, at[4] = {0, 0, 0, 0}, id, len, crc, i, k, n;
    if (!flash_ok)
        return;
    for (a = 0x93000u; a < BLE_VM_AT; a += 0x1000u)
        if (!st_read(a, h, 4) && h[0] == 0x55u && h[1] == 0xAAu && h[2] == 0xAAu && h[3] == 0x55u)
            break;
    if (a >= BLE_VM_AT)
        return;                                 /* (no VM below 0xE8000) */
    while (off + 4u <= BLE_VM_LOG_MAX && a + off + 4u <= BLE_VM_AT && !st_read(a + off, h, 4)) {
        id = h[1] | (h[2] & 15u) << 8, len = (uint32_t)(h[2] >> 4) | (uint32_t)h[3] << 4;
        if (off + 4u + len > BLE_VM_LOG_MAX || a + off + 4u + len > BLE_VM_AT)
            break;
        for (i = 0, crc = 0; i < len; i += k) {
            k = len - i < sizeof c ? len - i : sizeof c;
            if (st_read(a + off + 4u + i, c, k))
                return;
            crc = ble_crc16(crc, c, k);
        }
        if ((crc & 0xFFu) != h[0])
            break;                              /* (the log's end) */
        for (i = 0; i < 4u; i++)
            if (id == RID[i] && len == RLEN[i])
                at[i] = a + off;                /* (the last one wins) */
        off += 4u + len;
    }
    img[0] = 0x55u, img[1] = 0xAAu, img[2] = 0xAAu, img[3] = 0x55u;
    for (i = 0, n = 4; i < 4u; i++) {           /* the four records as they are */
        if (!at[i] || st_read(at[i], img + n, 4u + RLEN[i]))
            return;                             /* (a trim record missing: nothing written) */
        n += 4u + RLEN[i];
    }
    if (st_read(BLE_VM_AT, d, n) == 0) {
        for (k = 0; k < n && d[k] == img[k]; k++)
            ;
        if (k == n)
            return;                             /* (written already) */
    }
    if (!ble_vm_same(BLE_VM_AT, 0, 0x1000u, 1)) {
        uint32_t took;
        if (!ble_vm_same(BLE_VM_AT, a, 0x1000u, 0) || fl_erase4k(BLE_VM_AT, &took))
            return;                             /* (not GR!MM's copy: stock's own, left as it is) */
    }
    fl_write(BLE_VM_AT, img, n);
}
