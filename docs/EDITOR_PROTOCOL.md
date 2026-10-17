# GR!MM editor protocol

How an editor (a web page, `tools/fm1_editor_test.py`) talks to GR!MM on the FM-1 over USB-MIDI SysEx: read and
write the user's sounds, the slots A–D and the settings, read the factory sounds, set the tracks' sounds live, and
upload samples. The device side is `firmware/src/editor.c` (commands 20–23) and `firmware/src/samples.c` (11–15). The
read/write scheme follows SLOOP's editor backup ([sloop-fm1](https://github.com/isod89/sloop-fm1)
`web/EDITOR_PROTOCOL.md`, commands 34–36, GPL-3.0): objects by id, read by offset and count, written as BEGIN → DATA →
COMMIT. The live one (23 TRACK) follows SLOOP's live SET: values at once, while playing, never saved.

Protocol 1 (step 26a) has commands 20–22; protocol 2 (step 26b2) adds 23 TRACK. INFO says which.

## Frames

- Request: `F0 7D 47 52 <cmd> <args…> F7`. `7D` is the non-commercial ID, `47 52` is "GR" (Felucca's and SLOOP's are
  `46 4C`, so neither firmware takes the other's commands).
- Every request gets one reply with the same head and `<cmd>`. A frame of another head, or an unknown command, gets
  none. Send one request at a time and wait for its reply.
- Frames are at most 640 bytes between `F0` and `F7`.
- All bytes are 7-bit. Packings:
  - **pack7:** groups of up to 7 bytes, each group after one byte of their top bits (bit j = bit 7 of byte j).
  - **u35:** 5 × 7 bits, least significant first (offsets, lengths, CRCs).
  - **u14:** 2 × 7 bits, least significant first (counts).
- CRC: CRC-32 as zlib's (`zlib.crc32`), over the object's bytes.
- Multi-byte values inside objects are little endian.

## Return codes (commands 21–23)

| rc | Meaning |
| --- | --- |
| 0 | ok |
| 1 | arguments (unknown object, offset or count out of range, length or format not this firmware's, a factory sound written, DATA out of order, broken pack7) |
| 2 | the object is refused: its CRC, or its contents (a sound that is no sound, settings out of range, a slot whose first byte is not 0 or 1). Nothing changed |
| 3 | stop and silence first: the FM-1 only writes its flash while the sequencer stands and nothing sounds (no note, no release, no DELAY or SPACE tail). A COMMIT refused with 3 stays open: stop, then send COMMIT again |
| 4 | the flash write failed (the object may have changed in RAM; try again) |
| 5 | no BEGIN for this object (or the last BEGIN / DATA is more than 15 s old) |

## 20 INFO

Request: none. Reply, in order:

| Bytes | |
| --- | --- |
| 1 | protocol version (2; step 26a's GR!MM: 1, without TRACK) |
| 1 | object format (1). A PUT's BEGIN must name it |
| n + 1 | the firmware version, ASCII, 0-terminated ("1.0") |
| 1 | user sound slots (64) |
| 1 | a sound's size (64) |
| 1 | factory sounds (40) |
| 5 | a slot's size, u35 (2532) |
| 5 | the settings' size, u35 (168) |
| 1 | sample slots (4) |
| 1 | sample slots in use (holding a good sample) |

## Objects

| id | Object | Size | GET | PUT |
| --- | --- | --- | --- | --- |
| 0 | the settings | 168 | yes | yes |
| 1–4 | slots A–D | 2532 | yes | yes |
| 16–79 | user sounds 1–64 (id 16 + slot) | 64 | yes | yes |
| 80–119 | factory sounds 1–40 (id 80 + index, in the firmware's order) | 64 | yes | no |

A factory sound reads as a user sound's record, so it can be written into a user slot as it comes.

## 21 GET

Request: `obj`, `off` (u35), `count` (u14, 1–256; `off + count` within the object).

Reply: `obj`, `rc`, `off` (u35), `count` (u14), then (rc 0) the `count` bytes, pack7.

## 22 PUT

Request: `op`, `obj`, then by `op`:

| op | | Arguments |
| --- | --- | --- |
| 0 | BEGIN | `fmt` (INFO's format), `len` (u35, the object's size), `crc` (u35, CRC-32 of all `len` bytes) |
| 1 | DATA | `off` (u35, where the last DATA ended: 0, 256, …), pack7 bytes (1–256) |
| 2 | COMMIT | none |
| 3 | ABORT | none |

Reply: `op`, `obj`, `rc`.

- BEGIN and COMMIT need the sequencer stopped and silence (rc 3). Factory sounds can't be written (rc 1).
- COMMIT checks the length, the CRC and the contents, then saves through the FM-1's own saves, each sector read back.
  The header shows "EDITOR: SOUND 12 SAVED", "EDITOR: SLOT B SAVED", "EDITOR: SETTINGS SAVED", or "EDITOR:
  REFUSED".
- One PUT at a time; a new BEGIN drops the one before. While a PUT is open the FM-1's autosave waits (up to 15 s
  after the last DATA).
- A slot written here is stored; the project playing does not change until that slot is loaded.
- Settings written here apply at once (colour, MIDI channel, tune, screen off, SEQ MIDI OUT, the octave, MIDI LEARN's
  links).

## 23 TRACK (protocol 2)

A track's sound as it plays, read or set at once, as loading it on PRESETS does; nothing is saved (STORE on the FM-1,
or a PUT of a sound, saves). At any time, also while the sequencer plays and notes sound: no rc 3.

Request: `op`, `t` (the track, 0–3), then by `op`:

| op | | Arguments |
| --- | --- | --- |
| 0 | READ | none |
| 1 | SET | pack7 of a sound (64 bytes, below; its CRC right: 74 bytes), then, optionally, its name (ASCII, up to 15) |
| 2 | SELECT | none: track `t` becomes the selected one, as ALGORITHM turned on the FM-1 |

Reply: `op`, `t`, the selected track (0–3), `rc` (0, 1 arguments, 2 not a sound), then for READ the pack7 of track
`t`'s sound now (64 bytes: category 0, number 1, no name; its CRC right: 74 bytes), then its name as HOME's header
shows it (ASCII, up to 15; none: nothing, HOME shows the source).

- A track's name: the one SET gave it (the editor page sends the factory or user sound it loaded, "FROZEN HALL", or the
  name typed in), else the sound PRESETS or SOUNDS loaded on the FM-1 ("CRYPT PAD", a user sound's name or "PAD 03"),
  while the track's source is still that sound's; SET without a name: none.

- SET applies all of it, as a sound loaded on PRESETS: the source (a new one: the track's notes start again), its four
  knobs, ENV, LATCH, the chord, the LFO and, for FM, the track's patch (bytes 18–49; byte 18 without bit 7: the
  PATCH as built). A new FM ALGO starts the track's notes again, as on FM ALGO.
- The editor page sends a SET for every move of a control, one at a time (it waits for the reply), the newest value
  kept while one is on its way; it reads the selected track about twice a second to follow the FM-1's knobs.

## A sound (64 bytes)

| Byte | |
| --- | --- |
| 0 | `0x54` ("T") |
| 1 | CATEGORY: 0 DRONE, 1 PAD, 2 BELL, 3 ORGAN, 4 KEYS, 5 BASS, 6 TEXTURE, 7 VOICE, 8 OTHER |
| 2 | its number in the category (the name "DRONE 03"), 1–99 |
| 3 | the source: 0 VA, 1 FM, 2 SAMPLE, 3 VOICE, 4 WHEEL, 5 PHYS, 6 PHASE |
| 4–7 | the source's four knobs (SOUND page), each 0..its range |
| 8–11 | ENV: ATTACK, DECAY, SUSTAIN, RELEASE (0–127) |
| 12 | LATCH (0 / 1) |
| 13 | the chord byte (below) |
| 14–17 | LFO: RATE (0–127), DEPTH (0–100), DESTINATION, SHAPE (below) |
| 18–49 | FM: the track's patch (below), when the source is FM and byte 18 has bit 7 set; else the PATCH as built |
| 50–59 | its name (ASCII, up to 10, 0-padded; the editor page gives it): PRESETS, SOUNDS and HOME show it instead of "PAD 03" (a byte the font has not as a space). Saving a sound on the FM-1 over a slot keeps the name that slot had; into an empty slot, none |
| 60–63 | CRC-32 of bytes 0–59 |

An empty slot is 64 × `0xFF`. A PUT of 64 × `0xFF` empties the slot.

Source knob ranges: VA WAVE 0–2, DETUNE, CUTOFF, RESONANCE 0–127. FM PATCH 0–3, BRIGHT, DECAY, DETUNE 0–127. SAMPLE
SLOT 0–3, START, LENGTH 0–127, REVERSE 0–1. VOICE VOWEL 0–127, SHIFT 0–24, BUZZ, BREATH 0–127. WHEEL DRAWBARS 0–15,
PERC 0–6, CLICK, DRIVE 0–127. PHYS MODEL 0–3, STRUCTURE, DAMP, BOW 0–127. PHASE WAVE 0–7, WAVE2 0–8, DCW, DETUNE
0–127.

**The chord byte:** bit 7 chord mode on; bits 4–6 VOICING (0 CLOSE, 1 OPEN, 2 SPREAD, 3 +OCT, 4 DROP); bits 0–3 CHORD
TYPE (0 SCALE, 1 MIN, 2 MAJ, 3 SUS2, 4 SUS4, 5 FIFTH, 6 MIN7, 7 MAJ7, 8 DIM, 9 ADD9, 10 CLUSTER).

**LFO:** DESTINATION 0 PITCH, 1 VOLUME, 2 PAN, 3–6 the source's knobs 1–4, 7 REV SEND, 8 GRAIN SEND, 9 TRK CUTOFF.
SHAPE 0 SINE, 1 TRIANGLE, 2 SAW UP, 3 SAW DOWN, 4 SQUARE, 5 RANDOM, 6 DRIFT.

**An FM patch (32 bytes):** byte 0 ALGO (0–31) + `0x80` (0: none, the PATCH as built), byte 1 FEEDBACK (0–7), then a
row of 5 an operator, OP1 first: RATIO (0–31, 0 = 0.5), FINE, LEVEL, ATTACK, DECAY (0–99).

## A slot (2532 bytes)

| Bytes | |
| --- | --- |
| 0 | 1 used, 0 empty |
| 1–3 | 0 |
| 4–211 | the four tracks, 52 bytes each (below) |
| 212–1251 | the four sequences, 260 bytes each (below) |
| 1252–2403 | the four tracks' step locks, 288 bytes each (below) |
| 2404–2531 | the four tracks' FM patches, 32 bytes each (as in a sound) |

**A track (52 bytes):**

| Byte | |
| --- | --- |
| 0 | the source (as in a sound) |
| 1–28 | every source's four knobs: source s's knob k at 1 + 4 s + k (all seven kept) |
| 29 | 127 − CUTOFF (HOME's filter; 0: open) |
| 30 | RESONANCE (0–127) |
| 31 | FX − 100, signed (0: 100 %; −100..100 for 0..200 %) |
| 32 | 0 |
| 33–36 | ENV: ATTACK, DECAY, SUSTAIN, RELEASE |
| 37 | LATCH |
| 38–42 | LEVEL, DLY SEND, REV SEND, GRAIN SEND, PAN (0–127, PAN 64 the middle) |
| 43 | the chord byte |
| 44 | ARP: bit 7 on; bits 0–6 MODE (0 UP, 1 DOWN, 2 UP-DOWN, 3 RANDOM, 4 AS PLAYED, 5 CHORD) |
| 45–47 | ARP RATE (0–8: 1/1 1/2 1/4 1/8 1/16 1/32 1/4T 1/8T 1/16T), OCTAVES (1–4), GATE (10–100) |
| 48–51 | LFO: RATE, DEPTH, DESTINATION, SHAPE |

**A sequence (260 bytes):** byte 0 STEPS (1–64), 1 DIVISION (0–7), 2 SWING (0–127), 3 PLAY MODE; then 64 steps of 4
bytes: a 24-bit value, little endian (bits 0–6 NOTE, 0 = OFF; 7–13 CHANCE 0–100; 14–15 RATCHET − 1; 16–21 LENGTH − 1),
then the step's chord byte (0: a plain note; bit 7 set: a chord step).

**Step locks (288 bytes):** 64 × 4 values (step n's lock of the source's knob k at 4 n + k), then 32 bytes of masks:
step n's 4 bits in byte 256 + n / 2, the low nibble for even n, the high for odd (bit k: knob k locked).

## The settings (168 bytes)

| Byte | |
| --- | --- |
| 0 | the octave, signed, −2..2 |
| 1 | TUNE, signed, −50..50 cents |
| 2 | COLOUR: 0 RED, 1 AMBER, 2 TEAL, 3 VIOLET |
| 3 | MIDI CHANNEL: 0 "1-4" .. 12 "13-16", 13 ALL > SEL |
| 4 | SCREEN OFF: 0 NEVER, 1, 5, 15, 30 MIN |
| 5 | SEQ MIDI OUT (0 / 1) |
| 6 | MIDI LEARN's links in use (0–32) |
| 7–166 | 32 links of 5 bytes, the oldest first: CC, page, knob (0–3), track (0–3), x (SOUND: the source; GRID: the step; SONG: the section; FM ALGO's FINE, FM OP: the operator) |
| 167 | the settings' version (1) |

Pages (a link's page): 0 SOUND, 1 SOURCE, 2 ENV, 3 LFO, 4 SCALE, 5 CHORD, 6 SONG, 7 HOME, 8 MIXER 1, 9 MIXER 2,
14 ARP, 15 GRID, 16 PATTERN SETTINGS, 17 TAPE, 18 GRAIN, 19 DELAY, 20 SPACE, 23 FM ALGO, 24 FM OP (the others can't be
learned).

## Samples (11–16)

As before (`tools/fm1_sample_upload.py`, `firmware/src/samples.c`'s header): 11 BEGIN slot → slot, rc; 12 WRITE slot,
off (3 × 7 bits), pack7 data (≤ 256 B, from 512 on, in order) → slot, off, rc; 13 END slot, pack7 header (480 B) →
slot, rc; 14 ERASE slot → slot, rc; 15 INFO → slots, then per slot: state (0 empty, 1 ready, 2 checking), name
(0-terminated, up to 8: the slot header's), samples (3 × 7 bits), capacity (3 × 7 bits), then (step 26c) per slot its
root note (MIDI; 60 for an empty slot); 16 FACTORY (none) → rc: all four slots GR!MM's factory samples again (about
5 s; only while nothing sounds). Their rc: 0 ok, 1 arguments, 2 erase, 3 write, 4 not begun / out of order, 5 header
or CRC, 6 something sounds, 7 no flash. Before step 26b2, INFO came back cut short when the four slots all had long
names (and overran a buffer on the FM-1).

The factory samples (step 26c, `tools/gen_samples.py` from `assets/samples/`, CC0 1.0): URCHIN, LEAVE, NIKOLAI,
M-SOFT, as an upload makes them (root C4). At power-on a slot whose 72 KiB are all erased (0xFF) gets its own; a slot
that holds anything is never touched there. FACTORY, or SYSTEM 2's SAMPLES: RESTORE and OCT+ on the FM-1, write all
four.

## A backup

All sounds, slots and settings: GET objects 0, 1–4, 16–79 (256 bytes a GET). To restore, PUT each one (an empty sound
as 64 × `0xFF`), each COMMIT while stopped and silent. `tools/fm1_editor_test.py` does both and checks the result.
