/* SPDX-License-Identifier: GPL-3.0-only */
/* GR!MM change (V3.0): the FM-1's flash parts, the JEDEC ids (0x9F) the firmware knows. Both are 1 MiB parts with the same plain SPI NOR commands (0x03 / 0x0B read, 0x02 program, 0x20 4 KiB erase,
 * 0x05 / 0x35 status, 0x06 write enable), which is all this firmware uses:
 *   0x856014  Puya P25Q80, the FM-1 as first sold
 *   0xB36014  UC25HQ80, the FM-1 sold since the autumn of 2026
 * Any other id: the flash is left alone (the saves stay in RAM, the update loader refuses). The macro and the idea as SLOOP 2.8.5 (isod89/sloop-fm1, GPL-3.0-only: its fm1_flash_id.h; the issue
 * hugelton/Felucca #319). Used by the app (platform.c), the USB rescue (recovery.c) and the update loader (loader.c). */
#pragma once
#ifndef FL_JEDEC_OK
#define FL_JEDEC_OK(id) ((id) == 0x856014u || (id) == 0xB36014u)
#endif
