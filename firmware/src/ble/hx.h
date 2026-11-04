/* SPDX-License-Identifier: GPL-3.0-only */
/* HX: fm1-dock's BLE-MIDI, a unit of its own at a fixed place in the image (PLAN.md, "BLE-MIDI").
 * The stack is Optimist's (w0ts/optimist, firmware/src/ble/, GPL-3.0-only): written from the Core and
 * BLE-MIDI specifications, with its driver for the WL82 baseband engine. HX is hx.c around it.
 *
 * The host (dock.c) calls HX only through the header at HX_XIP, and only once the header's CRC is
 * good: a boot firmware may have written over that flash, as over the image's modules. HX may call
 * the host's code (it is always there), never the other way but through the header.
 *
 *   init(host, kept, on, failed)  once at boot, interrupts off: HX's data, the radio's stored trims
 *                                 (stock's VM, else the copy in kept), the two vectors (masked); with
 *                                 on (and the trims, and a boot that did not fail) the radio and
 *                                 advertising. -> HX_S_* of state()
 *   set(on)                       the main loop: BLUETOOTH ON / OFF (the first ON starts the radio)
 *   poll()                        TIMER5, 2 kHz: BLE MIDI in -> host->midi_in
 *   out(pkt)                      the audio ISR (usb.c midi_out_event): a USB-MIDI event packet out
 *   state()                       HX_S_*
 *   kept(rec)                     -> 1 when rec (the settings record) changed and should be saved
 *   stop()                        before flash under HX is written: the link and the interrupts off
 */
#ifndef HX_H
#define HX_H
#include <stdint.h>

#define HX_MAGIC 0x31584248u                    /* "HBX1" */
#define HX_ABI 1u
#if BOARD_SMK37
#define HX_AT 0x65000u                          /* flash: the top 40 KiB of the SMK-37's app area (to 0x6F000; its app slot ends at 0x6F0DC) */
#else
#define HX_AT 0x88000u                          /* flash: the top 40 KiB of the image area (to 0x92000; the app slot ends at 0x920DC) */
#endif
#define HX_SIZE 0xA000u
#define HX_XIP (0x02000000u + HX_AT - 0x4000u)
#define HX_KEPT 112u                            /* the settings record: [0..7] the address, [8..107] the trims copy */

enum {
    HX_S_OFF = 0,                               /* BLUETOOTH OFF */
    HX_S_NOCAL = 1,                             /* ON, but no stored trims: the radio is never started */
    HX_S_HELD = 2,                              /* ON, but this boot follows a failed one: not started */
    HX_S_ADV = 3,                               /* advertising */
    HX_S_LINK = 4,                              /* a central connected */
    HX_S_READY = 5                              /* ... and listening to the MIDI notifications */
};

typedef struct {
    int (*flash_read)(uint32_t off, void *dst, uint32_t n);   /* SPI, as stored -> 0 ok */
    int (*midi_in)(uint32_t pkt);               /* TIMER5: into the host's MIDI ring -> 1 taken */
} hx_host_t;

typedef struct {
    uint32_t magic, size, crc, abi;             /* crc: CRC-32 (zlib's) of the image from byte 16 to size */
    uint32_t (*init)(const hx_host_t *h, const uint8_t kept[HX_KEPT], uint32_t on, uint32_t failed);
    void (*set)(uint32_t on);
    void (*poll)(void);
    void (*out)(uint32_t pkt);
    uint32_t (*state)(void);
    int (*kept)(uint8_t kept[HX_KEPT]);
    void (*stop)(void);
} hx_api_t;
#endif
