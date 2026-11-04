# GR!MM editor protocol

How an editor (a web page, `tools/fm1_editor_test.py`) talks to GR!MM on the FM-1 over USB-MIDI SysEx: read and
write the user's sounds, the slots A–D and the settings, read the factory sounds, set the tracks' sounds live, and
upload samples (and, GR!MM V2, read them back). The device side is `firmware/src/editor.c` (commands 20–25) and `firmware/src/samples.c` (11–15). The
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
| n + 1 | the firmware version, ASCII, 0-terminated ("2.0"; GR!MM 1.0: "1.0") |
| 1 | user sound slots (64) |
| 1 | a sound's size (64) |
| 1 | factory sounds (40) |
| 5 | a slot's size, u35 (2532) |
| 5 | the settings' size, u35 (170; GR!MM 1.0: 168) |
| 1 | sample slots (4) |
| 1 | sample slots in use (holding a good sample) |

## Objects

| id | Object | Size | GET | PUT |
| --- | --- | --- | --- | --- |
| 0 | the settings | 170 (1.0: 168) | yes | yes |
| 1–4 | slots A–D | 2532 | yes | yes |
| 5–8 | sample slots 1–4 (GR!MM V2) | 512 + the data's length | yes | no |
| 9 | the working project as it is now (GR!MM V2 rc9) | 2576 | yes | no |
| 10 | the mix's change log (GR!MM V2 rc9) | 772 | yes | no |
| 16–79 | user sounds 1–64 (id 16 + slot) | 64 | yes | yes |
| 80–123 | factory sounds 1–44 (id 80 + index, in the firmware's order; GR!MM 1.0: 80–119) | 64 | yes | no |

An object is one 7-bit byte (0–127). V2's test builds before 2.0 had the sample slots at 120–123: backups store
samples by slot, not by object, so theirs restore.

A factory sound reads as a user sound's record, so it can be written into a user slot as it comes.

A sample slot reads as the flash holds it: the header (480 bytes, as an upload's END sends it), 32 bytes of nothing
(`0xFF`), then the ADPCM data; its length is the header's u32 at byte 16. Only a ready slot (INFO 15: state 1) is
there; an empty one, or one being checked, answers rc 1, as does a GR!MM before V2. Read the first 512 bytes, then the
data from 512. It is written back as an upload (11 BEGIN, 12 WRITE from 512, 13 END with the header), not by PUT.

The working project (9) is what the autosave keeps: the four tracks (LEVEL, the sends, CUTOFF, RESONANCE, FX, PAN, the
TAPE tile ...), the effects, tempo, the sequences, the song, the mutes and solos, the FM patches, MASTER and DRIVE,
packed as the save sector holds it (firmware/src/store.c pk_work). It is packed again for each GET (a value moving
meanwhile: the later chunks have it). While a PUT is open it answers rc 1. A backup (tools/fm1_editor_test.py) keeps
it as `work`, for reading only: a restore does not write it.

The change log (10) says what moved a track's mix lately: u32 the entries made so far, then 64 entries of 12 bytes,
the oldest overwritten (entry `n % 64` is the next): u32 the time (ms since power-on), u8 the source (0 a knob, 1 a
MIDI CC's link, 2 the editor, 3 a load: power-on, a slot, undo, a project), u8 the page (a knob's or link's; 255 a
load), u8 the value (0 LEVEL, 1 CUTOFF, 2 RESONANCE, 3 FX, 4 DLY, 5 REV, 6 GRAIN, 7 TAPE, 8 DRIVE, 9 PAN), u8 the
track (0–3), i16 from, i16 to. In RAM only: a power-off clears it.

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
| 12 | bit 0 LATCH (0 / 1); bits 1–7 GLIDE (V2: 0 OFF, 1–127: 10 ms × 2^(v × 12 / 192); saved before: 0) |
| 13 | the chord byte (below) |
| 14–17 | LFO: RATE (0–127), DEPTH (0–100), DESTINATION, SHAPE (below) |
| 18–49 | FM: the track's patch (below), when the source is FM and byte 18 has bit 7 set; else the PATCH as built. VOICE (V2): byte 18 its TYPE (0 SOLO, 1 GREGORIAN, 2 NUNS, 3 CATHEDRAL; saved before: 0). PHASE (V2): byte 18 bit 0 RING, bit 1 NOISE; 19 LINE 2's WAVE (0 SAME, 1–8: SAW .. RTRP); 20 its DCW (0 SAME, 1–127); 21 DCW ENV (0 OFF, 1–127); 22 its DECAY (0–127: 10 ms × 2^(v × 15 / 192)); 23 PITCH ENV (a signed byte, −64 … 63 semitones; 0 OFF); 24 its DECAY (as 22); saved before: 0, all off |
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
| 1–28 | every source's four knobs: source s's knob k at 1 + 4 s + k (all seven kept); byte 14 (VOICE's SHIFT, 0–24): bits 5–6 VOICE's TYPE (V2: 0 SOLO, 1 GREGORIAN, 2 NUNS, 3 CATHEDRAL; GR!MM 1.0: 0) |
| 29 | 127 − CUTOFF (HOME's filter; 0: open) |
| 30 | RESONANCE (0–127) |
| 31 | FX − 100, signed (0: 100 %; −100..100 for 0..200 %) |
| 32 | the filter's TYPE (0 LP, 1 HP, 2 BP) + 4 × POS (0 PRE, 1 POST) (MIXER 2, V2) + 8 × the TAPE SEND's low five bits (MIXER 1, V2) (GR!MM 1.0: 0) |
| 33–36 | ENV: ATTACK, DECAY, SUSTAIN, RELEASE |
| 37 | bit 0 LATCH; bits 1–7 the DRIVE SEND, 0–127 (MIXER 1, V2; GR!MM 1.0: 0) |
| 38–42 | LEVEL, DLY SEND, REV SEND, GRAIN SEND, PAN (0–127, PAN 64 the middle) |
| 43 | the chord byte |
| 44 | ARP: bit 7 on; bits 0–6 MODE (0 UP, 1 DOWN, 2 UP-DOWN, 3 RANDOM, 4 AS PLAYED, 5 CHORD) |
| 45–47 | ARP RATE (0–8: 1/1 1/2 1/4 1/8 1/16 1/32 1/4T 1/8T 1/16T), OCTAVES (1–4; bits 5–6: the TAPE SEND's top two bits, V2; GR!MM 1.0: 0), GATE (10–100) |
| 48–51 | LFO: RATE, DEPTH, DESTINATION, SHAPE |

**A sequence (260 bytes):** byte 0 STEPS (1–64), 1 DIVISION (0–7), 2 SWING (0–127), 3 PLAY MODE; then 64 steps of 4
bytes: a 24-bit value, little endian (bits 0–6 NOTE, 0 = OFF; 7–13 CHANCE 0–100; 14–15 RATCHET − 1; 16–21 LENGTH − 1),
then the step's chord byte (0: a plain note; bit 7 set: a chord step).

**Step locks (288 bytes):** 64 × 4 values (step n's lock of the source's knob k at 4 n + k), then 32 bytes of masks:
step n's 4 bits in byte 256 + n / 2, the low nibble for even n, the high for odd (bit k: knob k locked).

## The settings (170 bytes; GR!MM 1.0: 168)

| Byte | |
| --- | --- |
| 0 | the octave, signed, −2..2 |
| 1 | TUNE, signed, −50..50 cents |
| 2 | COLOUR: 0 RED, 1 AMBER, 2 TEAL, 3 VIOLET |
| 3 | MIDI CHANNEL: 0 "1-4" .. 12 "13-16", 13 ALL > SEL, 14 .. 29 "CH 1 > SEL" .. "CH 16 > SEL" (V2) |
| 4 | SCREEN OFF: 0 NEVER, 1, 5, 15, 30 MIN |
| 5 | SEQ MIDI OUT (0 / 1) |
| 6 | MIDI LEARN's links in use (0–32) |
| 7–166 | 32 links of 5 bytes, the oldest first: CC, page, knob (0–3), track (0–3), x (SOUND: the source; GRID: the step; SONG: the section; FM ALGO's FINE, FM OP: the operator; MIXER 1: 4 the DRIVE SEND, 5 the TAPE SEND, else knob 0 LEVEL, 1 DLY SEND, 2 REV SEND, 3 GRAIN SEND, as GR!MM 1.0's knobs) |
| 167 | the settings' version (3; GR!MM 1.0's: 1; V2's first builds': 2) |
| 168 | CLOCK (V2): 0 INT, 1 INT+OUT, 2 USB, 3 TRS |
| 169 | SYSTEM 3 (V2; version 3): bit 0 LATCH: 0 FROM SOUND (a sound loaded brings its own LATCH, as 1.0), 1 ALWAYS OFF (a sound loaded sets the track's LATCH off); bit 1 VELOCITY: 0 OFF (as before), 1 ON (a MIDI note's volume from its velocity) |

A PUT of the settings also takes GR!MM 1.0's 168 bytes (version 1): they apply with CLOCK INT and LATCH FROM SOUND,
so a backup of then restores; and version 2's 169 bytes (LATCH FROM SOUND).

Pages (a link's page): 0 SOUND, 1 SOURCE, 2 ENV, 3 LFO, 4 SCALE, 5 CHORD, 6 SONG, 7 HOME, 8 MIXER 1, 9 MIXER 2,
14 ARP, 15 GRID, 16 PATTERN SETTINGS, 17 TAPE, 18 GRAIN, 19 DELAY, 20 SPACE, 23 FM ALGO, 24 FM OP, 25 MASTER, 26 DRIVE
(the others can't be learned; 27 is SYSTEM 3).

## 24 CPU (GR!MM V2, a diagnostic)

Where the audio time goes, as SYSTEM 2's CPU row measures it; `tools/fm1_cpu.py` prints it. It has no protocol number
of its own (INFO stays 2); a GR!MM before V2 does not answer it.

Request: none (rc11: or the byte 1, below). Reply: `n` (the stages, 10), then u35 values in the FM-1's timer ticks (24 a microsecond): the budget of a
256-frame block (139320, 5.8 ms), NOW, PEAK, then NOW's split over the same ~0.5 s: the sources (VOICES), the tracks'
filters, levels and sends (MIX), GRAIN, DELAY, SPACE, TAPE, PERFORM, MASTER, DRIVE, and SCAN (the key scan's timer
interrupts that land inside the audio, taken out of the stages they interrupt). NOW minus them all is the rest of the
audio callback. Then (rc9) one more u35: the USB recording's dropouts since power-on, then (rc11) 25 more, the FM-1's
health: the USB audio ring dry, full, no packet ready, the host stopped reading; USB bus resets, suspends, frames frozen;
the longest time between two watchdog feeds (it bites at ~8 s), the longest gap between two key scan ticks (100 us),
the ticks past 0.2 ms, 1 ms, 10 ms, the longest tick; the flash's longest interrupts-off time and how often past 1 ms
(timer ticks); the battery divider's lowest and highest reading; starts since power-on, the resets nobody asked for
(RAM kept over them, no restart of the FM-1's own before: an update, USB boot, the fault screen), of them the
watchdog's and the supply-low ones (a supply that sagged); the last four starts' P33 RST_SRC bytes (01 power-on, 02
VDDIO low, 04 watchdog, 08 VCM, 10 long press, 20 1.2 V low, 40 soft; the chip keeps the watchdog's after a restart
of the FM-1's own, so it alone proves nothing); this start's byte, with its kind above it (1 watchdog, 2 low
voltage); the faults; the uptime in ms. A request with one argument byte 1 starts the peaks and counts over
(`tools/fm1_cpu.py --clear`; `--health` prints them).

## 25 FX (GR!MM V2)

The project's effects, live, for the editor page's FX section (no protocol number of its own; a GR!MM before V2 does
not answer it). A value set here acts at once and is kept by the project (the FM-1's autosave), as a knob's turn.

Request: `op`, then for SET `i`, `v`.

| op | | Reply |
| --- | --- | --- |
| 0 | READ | `op`, `rc`, then 48 values (V2 builds before MIXER 1's TAPE SEND: 44; before its DRIVE SEND: 28) |
| 1 | SET `i` to `v` | `op`, `rc` |

The values, in order: TAPE (WOW, SATURATION, TONE, HISS), GRAIN (SIZE, DENSITY, SCATTER, MIX), DELAY (TIME, FEEDBACK,
WEAR, MIX), SPACE (SIZE, DAMP, MIX, PRE-DELAY), MASTER (CUTOFF, RESONANCE, TYPE 0 LP / 1 HP / 2 BP, POSITION 0 PRE FX /
1 POST REVERB / 2 END), DRIVE (AMOUNT, TYPE 0 SOFT / 1 FUZZ / 2 CLIP / 3 FOLD, TONE, MIX): each 0..127 (or its list),
as the FM-1's knobs; then T1..T4's filter (MIXER 2): TYPE (0 LP, 1 HP, 2 BP) + 4 × POS (0 PRE, 1 POST); then T1..T4's
sends (MIXER 1), four a track: DLY, REV, GRAIN, DRIVE (0..127; value 28 + 4 × track + send); then T1..T4's TAPE
sends (0..127; value 44 + track). A value out of its range is set to the nearest end.

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
M-SOFT, as an upload makes them (root C4). At power-on a slot whose 72 KiB are all erased (0xFF) gets its own; (V2.0.2)
once the slots have been checked and nothing sounds, so does every slot that still does not play (state 0: no good
header, or the data's CRC wrong), once per boot. A slot that plays is never touched. FACTORY, or SYSTEM 2's SAMPLES:
RESTORE and OCT+ on the FM-1, write all four.

## A backup

All sounds, slots and settings: GET objects 0, 1–4, 16–79 (256 bytes a GET); GR!MM V2: the sample slots too, objects
5–8 (an empty slot: none). To restore, PUT each one (an empty sound as 64 × `0xFF`), each COMMIT while stopped
and silent; then each sample slot as an upload (11–13; one whose header already matches, its data's CRC in it, need
not be written again; one empty in the backup: 14 ERASE). `tools/fm1_editor_test.py` and the editor page do both
and check the result. The backup file (theirs): JSON, `{"format": "grimm-backup", "version": 1, "proto", "fmt",
"firmware", "date", "objects": [{"id", "len", "crc", "data"}], "samples": [4 × null or {"slot", "name", "len", "crc",
"data"}]}`, data in base64, `crc` the CRC-32 of the bytes. `samples` is V2's: a file without it restores everything
else and leaves the sample slots alone.

Requests may follow each other back to back: the FM-1 holds one request at a time and drops one it has not taken
within a second (GR!MM 1.0 and V2 before the fix could drop a fresh one in a long run of requests: a timer kept from an
earlier one). A GET changes nothing, so a reply that does not come (3 s) can simply be asked again; the tools ask up
to three times and say how many they asked again (a healthy link: 0).
