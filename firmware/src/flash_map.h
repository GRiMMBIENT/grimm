/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* The flash map the app uses (platform.c, flash_store.c, tests/flash_chip_test.c): where the settings, the samples and the saves are. */
#pragma once
#define SETTINGS_AT 0x000FC000u                 /* one 4 KiB sector of Felucca's globals area */
#define SAMPLE_AREA 0x00097000u                 /* GR!MM: the sample slots (samples.c) */
#define SAMPLE_AREA_LEN 0x00048000u             /* 288 KiB: up to 0xDF000 */
/* GR!MM's saves (store.c): six sectors where Felucca keeps data an app writes (its storage.c flash map: its preset banks'
 * last sector, its autosave pair and voice bank above the update's staging, its globals beside the settings), none in the
 * sample area */
#define SAVE_NSECT 6u
static const uint32_t SAVE_SECT[SAVE_NSECT] = {0xDF000u, 0xE5000u, 0xE6000u, 0xE7000u, 0xFD000u, 0xFE000u};
