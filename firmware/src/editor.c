/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 GR!MM */
/* GR!MM's editor mailbox: a web editor (or tools/fm1_editor_test.py) reads and writes the FM-1's sounds, slots A..D
 * and settings over USB SysEx, on the sample upload's head (F0 7D 47 52 <cmd> .. F7, pack7: samples.c), commands
 * 20..22 beside the samples' 11..15. The format: docs/EDITOR_PROTOCOL.md. Its approach is SLOOP's editor backup
 * (isod89/sloop-fm1, web/EDITOR_PROTOCOL.md, cmds 34..36, GPL-3.0): objects by id, read by offset and count, written
 * as BEGIN (length, CRC-32) -> DATA (in order) -> COMMIT; GR!MM's own code, its own ids and a format version.
 *   20 INFO                    -> the protocol, the format, the firmware's version, the sizes, the sample slots used
 *   21 GET  obj, off, count    -> obj, rc, off, count, pack7 bytes (count 1..256)
 *   22 PUT  op 0 BEGIN obj, fmt, len, crc / 1 DATA obj, off, pack7 (<= 256 B, in order) / 2 COMMIT obj / 3 ABORT obj
 *                              -> op, obj, rc
 *   23 TRACK op 0 READ t / 1 SET t, pack7 sound (64 B) [, its name] / 2 SELECT t
 *                              -> op, t, the selected track, rc (READ: then pack7 track t's sound now, then its name
 *                                 as HOME shows it, if it has one)
 *   24 CPU                     -> the budget, NOW, PEAK, then NOW split by stage (grimm.c cpu_stage), u35 fm1_ticks
 *                              each (GR!MM V2: a diagnostic, tools/fm1_cpu.py; no protocol number of its own); rc9:
 *                              then u35 the USB recording's dropouts since power-on
 *   25 FX   op 0 READ          -> op, rc, the project's effects as their pages' knobs (TAPE, GRAIN, DELAY, SPACE, MASTER,
 *                                 DRIVE: four each, 24), the tracks' filter TYPE | POS << 2 (4), the tracks' sends DLY,
 *                                 REV, GRAIN, DRIVE (16: T1's four first), their TAPE sends (4) (an older page reads
 *                                 the first 28, or 44)
 *           op 1 SET i, v      -> op, rc: value i (0..47, as READ's) to v, live (the project's: the autosave keeps it)
 *                              (GR!MM V2, the editor page's FX; no protocol number of its own either)
 * TRACK is live (protocol 2): a track's sound as it plays, read or set at once, as PRESETS loads one (snd_apply), never
 * saved; at any time, also while playing (the editor page's sliders, SLOOP's live SET). The page sends one at a time.
 * Objects: 0 the settings; 1..4 slots A..D; (V2) 5..8 the sample slots 1..4 as the flash holds them, a ready one's
 * header, data and all (GET only: a backup's; a restore writes them as an upload, 11..13); 16..79 the user's sounds
 * 1..64; 80.. the factory sounds (GET only; up to 127: an object is one 7-bit byte).
 * A PUT's bytes gather in store.c's sector image (st_img: free between saves; a reply is made in its last KiB); it
 * commits only when the sequencer stands and nothing sounds, its CRC, size, format and contents checked, through
 * store.c's saves (each written sector read back); refused, nothing changes. While a PUT is open (15 s after its last
 * DATA) the autosave waits (grimm.c: ed_open). Main loop only (samples_poll). */

#define ED_PROTO 2u                             /* this protocol's version (INFO; 2: TRACK) */
#define ED_FMT 1u                               /* the objects' format: sounds of 64 B, slots of SLOT_ALL, settings
                                                 * as set (SET_VER 3; a PUT of V1.0's, 1, and of version 2 too) */
enum { ED_INFO = 20, ED_GET = 21, ED_PUT = 22, ED_TRACK = 23, ED_CPU = 24, ED_EFFECTS = 25 };
static const uint8_t ED_FXPG[6] = {PG_TAPE, PG_GRAIN, PG_DELAY, PG_SPACE, PG_MASTER, PG_DRIVE};   /* (FX's i / 4) */
#define ED_SND7 74u                             /* a sound (64 B) in pack7 */
enum { EO_SET = 0, EO_SLOT = 1, EO_SMP = 5, EO_WORK = 9, EO_LOG = 10, EO_SND = 16, EO_FACT = 80 };
_Static_assert(EO_SMP + SMP_SLOTS <= EO_SND && EO_FACT + SND_NFACT <= 128u, "the editor's objects: one 7-bit byte each");
enum { ER_OK, ER_ARGS, ER_BAD, ER_STOP, ER_FLASH, ER_NOBEGIN };   /* rc: arguments, the object (CRC, size, format,
                                                 * contents), stop / silence first, the flash, no BEGIN (or 15 s old) */
#define ED_RX (st_img)                          /* a PUT's bytes (up to SLOT_ALL) */
#define ED_FX (st_img + 2560u)                  /* a factory sound as a 64 B record */
#define ED_TX (st_img + 3072u)                  /* a reply */
_Static_assert(SLOT_ALL <= 2560u && 6u + 12u + 293u + 1u <= 1024u, "the editor's room in st_img");
_Static_assert(WORK_PK <= 3072u && EO_SMP + SMP_SLOTS <= EO_WORK, "the working project read in st_img, under the reply");
static int32_t ed_obj = -1;                     /* the PUT begun (its object), -1 none */
static uint32_t ed_len, ed_crc, ed_got, ed_t;

static int ed_open(void) { return ed_obj >= 0 && fm1_ms - ed_t < 15000u; }
static uint32_t ed_size(uint32_t o)             /* object o's size (0: none) */
{
    if (o == EO_SET) return sizeof set;
    if (o >= EO_SLOT && o < EO_SLOT + 4u) return SLOT_ALL;
    if ((o >= EO_SND && o < EO_SND + SND_N) || (o >= EO_FACT && o < EO_FACT + SND_NFACT)) return SND_SIZE;
    if (o >= EO_SMP && o < EO_SMP + SMP_SLOTS && smp[o - EO_SMP].ready) return SMP_DATA_OFF + smp[o - EO_SMP].len;
    if (o == EO_WORK) return ed_open() ? 0u : WORK_PK;   /* (rc9: GET only; not while a PUT's bytes are in st_img) */
    if (o == EO_LOG) return sizeof clog + 4u;
    return 0;                                   /* (V2: a sample slot: its header, data; not ready: none) */
}
static const uint8_t *ed_bytes(uint32_t o)      /* object o as it is now (0: no flash) */
{
    uint32_t k;
    if (o == EO_SET) return (const uint8_t *)&set;
    if (o == EO_WORK) {                         /* (rc9) the working project as it is now (as the autosave packs it;
                                                 * packed again for each GET) */
        pk_work(st_img);
        return st_img;
    }
    if (o == EO_LOG) {                          /* (rc9) the mix's change log: entries made (u32), then clog[] */
        uint32_t n = clog_n;
        st_img[0] = (uint8_t)n, st_img[1] = (uint8_t)(n >> 8), st_img[2] = (uint8_t)(n >> 16), st_img[3] = (uint8_t)(n >> 24);
        memcpy(st_img + 4, clog, sizeof clog);
        return st_img;
    }
    if (o >= EO_SMP && o < EO_SND) return sample_flash() ? sample_flash() + (o - EO_SMP) * SMP_SLOT_SIZE : 0;
    if (o < EO_SND) return slots[o - EO_SLOT];
    if (o < EO_FACT) return save_flash(0) ? save_flash(0) + (o - EO_SND) * SND_SIZE : 0;
    for (k = 0; k < SND_SIZE; k++)              /* (a factory sound: as a user's record, no FM patch of its own; V2:
                                                 * byte 18 VOICE's TYPE, 0 in the others) */
        ED_FX[k] = k < SND_FSIZE && k < 19u ? SND_FACTORY[o - EO_FACT][k] : 0u;
    ED_FX[0] = SND_MAGIC;
    st_wr32(ED_FX + 60, st_crc(ED_FX, 60));
    return ED_FX;
}
static uint32_t ed_rd35(const uint8_t *p)
{
    return p[0] | (uint32_t)p[1] << 7 | (uint32_t)p[2] << 14 | (uint32_t)p[3] << 21 | (uint32_t)p[4] << 28;
}
static uint8_t *ed_wr35(uint8_t *p, uint32_t v)
{
    uint32_t k;
    for (k = 0; k < 5u; k++) *p++ = (uint8_t)((v >> (7u * k)) & 0x7Fu);
    return p;
}
static void ed_send(uint32_t cmd, const uint8_t *a, uint32_t n, const uint8_t *d, uint32_t dn, const char *tail)
{                                               /* head, a, pack7 d, tail (ASCII, 0: none) */
    uint8_t *w = ED_TX;
    uint32_t i, j;
    *w++ = 0xF0, *w++ = 0x7D, *w++ = 0x47, *w++ = 0x52, *w++ = (uint8_t)cmd;
    for (i = 0; i < n; i++) *w++ = a[i] & 0x7Fu;
    for (i = 0; i < dn; i += 7u) {              /* pack7: a byte of top bits, then up to 7 bytes */
        uint32_t g = dn - i < 7u ? dn - i : 7u, top = 0;
        for (j = 0; j < g; j++) top |= (uint32_t)(d[i + j] >> 7) << j;
        *w++ = (uint8_t)top;
        for (j = 0; j < g; j++) *w++ = d[i + j] & 0x7Fu;
    }
    while (tail && *tail) *w++ = (uint8_t)*tail++ & 0x7Fu;
    *w++ = 0xF7;
    sysex_send(ED_TX, (uint32_t)(w - ED_TX));
}

/* COMMIT: the bytes gathered, checked as object o; saved. ER_OK, or why not (nothing changed) */
static uint32_t ed_commit(uint32_t o)
{
    char m[24];
    if (o == EO_SET) {                          /* the settings: as set, its ranges */
        __typeof__(set) s;
        uint32_t i;
        memcpy(&s, ED_RX, sizeof s);
        if (ed_len == sizeof s - 2u) {          /* (V1.0's: version 1, no CLOCK: INT, no LATCH: OFF) */
            if (s.ver != 1u) return ER_BAD;
            s.ver = SET_VER, s.clk = CK_INT, s.latch = 0;
        } else if (ed_len == sizeof s - 1u) {   /* (V2's version 2: no LATCH, OFF) */
            if (s.ver != 2u) return ER_BAD;
            s.ver = SET_VER, s.latch = 0;
        }
        if (s.ver != SET_VER || s.oct < -2 || s.oct > 2 || s.tune < -50 || s.tune > 50 || s.col > 3u ||
            s.mch > MCH_MAX || s.scr > 4u || s.out > 1u || s.nlk > NLINK || s.clk >= CK_N || s.latch > 3u)
            return ER_BAD;
        for (i = 0; i < s.nlk; i++)
            if (s.lk[i][1] >= NPAGE || s.lk[i][2] > 3u || s.lk[i][3] > 3u) return ER_BAD;
        sy_col = s.col, sy_mch = s.mch, sy_tune = s.tune, sy_scr = s.scr, sy_out = s.out, sy_clk = s.clk, sy_latch = s.latch & 1u, sy_vel = s.latch >> 1 & 1u;
        sys_sync();                             /* (a new MIDI CHANNEL: what is on out there let go first) */
        set.oct = s.oct, set.nlk = s.nlk, set.ver = SET_VER;
        memcpy(set.lk, s.lk, sizeof set.lk);
        learn_st = 0, sy_learn = LEARN_OFF;
        set_dirty = 0;
        if (settings_save(&set, sizeof set)) return ER_FLASH;
        hd_say("EDITOR: SETTINGS SAVED");
        return ER_OK;
    }
    if (o < EO_SND) {                           /* a slot: used or not, then into the stream (its sectors read back) */
        if (ED_RX[0] > 1u) return ER_BAD;
        memcpy(slots[o - EO_SLOT], ED_RX, SLOT_ALL);
        slot_dirty |= 1u << (o - EO_SLOT);
        if (slots_save()) return ER_FLASH;
        put_s(m, "EDITOR: SLOT A SAVED"), m[13] = (char)('A' + o - EO_SLOT);
        hd_say(m);
        return ER_OK;
    }
    {                                           /* a sound: a good record, or all 0xFF (the slot emptied) */
        uint8_t r[SND_SIZE];
        uint32_t k, empty = 1;
        memcpy(r, ED_RX, SND_SIZE);             /* (snd_put makes its sector in st_img) */
        for (k = 0; k < SND_SIZE; k++) empty &= r[k] == 0xFFu;
        if (!empty && !snd_ok(r)) return ER_BAD;
        if (snd_put(o - EO_SND, empty ? 0 : r)) return ER_FLASH;
        put_s(put_u(put_s(m, "EDITOR: SOUND "), o - EO_SND + 1u), " SAVED");
        hd_say(m);
        return ER_OK;
    }
}

/* a frame (7-bit bytes after F0, the head included): 1 if it was the editor's; silent: stopped, nothing sounds */
static int ed_frame(const uint8_t *f, uint32_t n, int silent)
{
    uint8_t r[16];
    uint32_t cmd, o, rc = ER_OK;
    if (n < 4u || f[1] != 0x47 || f[2] != 0x52 || f[3] < ED_INFO || f[3] > ED_EFFECTS) return 0;
    cmd = f[3], f += 4, n -= 4;
    if (cmd == ED_INFO) {                       /* proto, fmt, the version (0-ended), sounds, a sound's size, the
                                                 * factory's, a slot's size, the settings' size, sample slots, used */
        uint8_t a[48], k, used = 0;
        const char *v = APP_VER;
        uint32_t i = 0;
        for (k = 0; k < SMP_SLOTS; k++) used += smp[k].ready;
        a[i++] = ED_PROTO, a[i++] = ED_FMT;
        while (*v && i < 20u) a[i++] = (uint8_t)*v++;
        a[i++] = 0, a[i++] = SND_N, a[i++] = SND_SIZE, a[i++] = SND_NFACT;
        ed_wr35(a + i, SLOT_ALL), i += 5;
        ed_wr35(a + i, sizeof set), i += 5;
        a[i++] = SMP_SLOTS, a[i++] = used;
        ed_send(cmd, a, i, 0, 0, 0);
        return 1;
    }
    if (cmd == ED_CPU) {                        /* -> the stages' count, then u35: the budget, NOW, PEAK, each stage */
        uint8_t a[1 + 5u * (4u + CS_N + HL_N)], *w = a + 1;
        uint32_t k, hl[HL_N];
        if (n && f[0] == 1u) hl_clear();        /* (rc11: asked with 1: the peaks and counts start over) */
        a[0] = CS_N;
        w = ed_wr35(w, BUDGET_TICKS), w = ed_wr35(w, cpu_now), w = ed_wr35(w, cpu_peak);
        for (k = 0; k < CS_N; k++) w = ed_wr35(w, cpu_stage[k]);
        w = ed_wr35(w, usb_drops());            /* (rc9: then the USB recording's dropouts since power-on) */
        hl_read(hl);                            /* (rc11: then the health values, platform.c hl_read) */
        for (k = 0; k < HL_N; k++) w = ed_wr35(w, hl[k]);
        ed_send(cmd, a, (uint32_t)(w - a), 0, 0, 0);
        return 1;
    }
    if (cmd == ED_EFFECTS) {                    /* op [, i, v] -> op, rc [, the 48 values] */
        uint8_t a[2u + 48u];
        uint32_t op = n ? f[0] : 2u, i, t;
        a[0] = (uint8_t)op, a[1] = ER_OK;
        if (op == 1u && n >= 3u && f[1] < 48u) {
            i = f[1];
            if (i >= 44u) {                     /* (V2: MIXER 1's TAPE sends; rc9: logged) */
                int32_t was = trk[i - 44u].tap;
                trk[i - 44u].tap = f[2] & 0x7Fu;
                clog_p(CL_EDITOR, PG_MIX1, &trk[i - 44u].tap, was);
            } else if (i >= 28u) {              /* (V2: MIXER 1's sends) */
                volatile int32_t *p = mx_send((i - 28u) / 4u, (i - 28u) % 4u);
                int32_t was = *p;
                *p = f[2] & 0x7Fu;
                clog_p(CL_EDITOR, PG_MIX1, p, was);
            }
            else if (i >= 24u) t = i - 24u, trk[t].ftype = clamp(f[2] & 3u, 0, NFTYPE - 1), trk[t].fpos = f[2] >> 2 & 1u;
            else {
                uint32_t pg = ED_FXPG[i / 4u], k = i % 4u;
                volatile int32_t *p = knob_param(pg, k);
                if (p) *p = clamp(f[2], knob_min(pg, k), knob_max(pg, k));
            }
            ed_send(cmd, a, 2, 0, 0, 0);
            return 1;
        }
        if (op != 0u) {
            a[1] = ER_ARGS;
            ed_send(cmd, a, 2, 0, 0, 0);
            return 1;
        }
        for (i = 0; i < 24u; i++) {
            volatile int32_t *p = knob_param(ED_FXPG[i / 4u], i % 4u);
            a[2u + i] = (uint8_t)clamp(p ? *p : 0, 0, 127);
        }
        for (t = 0; t < NT; t++) a[26u + t] = (uint8_t)(clamp(trk[t].ftype, 0, NFTYPE - 1) | clamp(trk[t].fpos, 0, 1) << 2);
        for (i = 0; i < 16u; i++) a[30u + i] = (uint8_t)clamp(*mx_send(i / 4u, i % 4u), 0, 127);
        for (t = 0; t < NT; t++) a[46u + t] = (uint8_t)clamp(trk[t].tap, 0, 127);
        ed_send(cmd, a, 50, 0, 0, 0);
        return 1;
    }
    if (cmd == ED_GET) {                        /* obj, off (5), count (2) -> obj, rc, off, count, the bytes */
        uint32_t off = n >= 8u ? ed_rd35(f + 1) : 0u, c = n >= 8u ? f[6] | (uint32_t)f[7] << 7 : 0u, sz;
        const uint8_t *b = 0;
        o = n ? f[0] : 0u;
        sz = ed_size(o);
        if (n < 8u || !sz || !c || c > 256u || off > sz || c > sz - off) rc = ER_ARGS;
        else if (!(b = ed_bytes(o))) rc = ER_FLASH;
        r[0] = (uint8_t)o, r[1] = (uint8_t)rc;
        ed_wr35(r + 2, off), r[7] = (uint8_t)(c & 0x7Fu), r[8] = (uint8_t)(c >> 7 & 0x7Fu);
        ed_send(cmd, r, 9, rc ? 0 : b + off, rc ? 0u : c, 0);
        return 1;
    }
    if (cmd == ED_TRACK) {                      /* op, t [, pack7 sound, name] -> op, t, sel, rc [, pack7 track t's
                                                 * sound, its name] */
        uint8_t s[SND_SIZE];
        char nm[24];
        uint32_t op = n >= 2u ? f[0] : 3u, t = n >= 2u ? f[1] : 0u, a;
        if (op > 2u || t >= NT) rc = ER_ARGS;
        else if (op == 1u) {                    /* SET: a good record, its source's; a new ALGO: its notes again;
                                                 * the name after it (HOME shows it), what the font has */
            if (n < 2u + ED_SND7 || unpack7(f + 2, ED_SND7, s, SND_SIZE) != SND_SIZE || !snd_ok(s)) rc = ER_BAD;
            else {
                uint32_t k, m = n - 2u - ED_SND7 < 15u ? n - 2u - ED_SND7 : 15u;
                a = (uint32_t)trk[t].fm_alg;
                snd_apply(s, t);
                if ((uint32_t)trk[t].fm_alg != a && trk[t].eng == ENG_FM) nq_put(NQ_RESTART, t, 0);
                for (k = 0; k < m; k++) trk_nm[t][k] = f[2 + ED_SND7 + k] >= 32u ? (char)f[2 + ED_SND7 + k] : ' ';
                trk_nm[t][m] = 0, trk_nm_e[t] = trk[t].eng, brw_pos[t] = -1;
            }
        } else if (op == 2u)
            sel = t;
        nm[0] = 0;
        if (!rc && !op) snd_pack(s, t, 0, 1), trk_sname(t, nm);
        r[0] = (uint8_t)op, r[1] = (uint8_t)t, r[2] = (uint8_t)sel, r[3] = (uint8_t)rc;
        ed_send(cmd, r, 4, s, !rc && !op ? SND_SIZE : 0u, nm);
        return 1;
    }
    {                                           /* PUT: op, obj .. -> op, obj, rc */
        uint32_t op = n >= 2u ? f[0] : 4u;
        o = n >= 2u ? f[1] : 0u;
        r[0] = (uint8_t)op, r[1] = (uint8_t)o;
        if (op == 0u) {                         /* BEGIN: fmt, len (5), crc (5) */
            uint32_t len = n >= 13u ? ed_rd35(f + 3) : 0u;
            if (n < 13u || !ed_size(o) || o >= EO_FACT || (o >= EO_SMP && o < EO_SND) || f[2] != ED_FMT ||
                (len != ed_size(o) && !(o == EO_SET && len + 2u >= sizeof set && len < sizeof set))) rc = ER_ARGS;
                                                /* (the settings: V1.0's, V2's version 2 too, a backup of then) */
            else if (!silent) rc = ER_STOP;
            else ed_obj = (int32_t)o, ed_len = len, ed_crc = ed_rd35(f + 8), ed_got = 0, ed_t = fm1_ms;
        } else if (op == 1u) {                  /* DATA: off (5), pack7, where the last ended */
            uint32_t off = n >= 7u ? ed_rd35(f + 2) : 0u, d;
            if (!ed_open() || (int32_t)o != ed_obj) rc = ER_NOBEGIN;
            else if (n < 8u || off != ed_got) rc = ER_ARGS;
            else if ((d = unpack7(f + 7, n - 7u, ED_RX + ed_got, ed_len - ed_got < 256u ? ed_len - ed_got : 256u)) >
                         (ed_len - ed_got < 256u ? ed_len - ed_got : 256u) || !d)
                rc = ER_ARGS;                   /* (unpack7: too much or a broken group) */
            else ed_got += d, ed_t = fm1_ms;
        } else if (op == 2u) {                  /* COMMIT */
            if (!ed_open() || (int32_t)o != ed_obj) rc = ER_NOBEGIN;
            else if (!silent) rc = ER_STOP;     /* (the PUT stays open: stop, then COMMIT again) */
            else {
                if (ed_got != ed_len || st_crc(ED_RX, ed_len) != ed_crc) rc = ER_BAD;
                else rc = ed_commit(o);
                ed_obj = -1;
                if (rc == ER_BAD) hd_say("EDITOR: REFUSED");
            }
        } else if (op == 3u) {                  /* ABORT */
            if ((int32_t)o == ed_obj) ed_obj = -1;
        } else
            rc = ER_ARGS;
        r[2] = (uint8_t)rc;
        ed_send(cmd, r, 3, 0, 0, 0);
        return 1;
    }
}
