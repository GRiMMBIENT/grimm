// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GR!MM
// The browser build against host mode: runs web/dist/grimm.wasm as web/worklet.js does (Asyncify), one millisecond at
// a time, with firmware/src/host.c's script, and compares with what tests/run_host.sh made:
//   - the sound, sample for sample (GR!MM V2: the app applies MASTER, fm1_master: tests/run_host.sh renders at the
//     knob's 1022, as here; its 24-bit words, build/host/grimm.raw, rounded as the page's floats are here);
//   - the screen at 2500, 3500, 6000 and 15500 ms, pixel for pixel (build/host/grimm-*.png), all but the CPU row: it shows the
//     longest app_audio() as this computer's clock measured it (fm1_ticks), which is not the same twice;
//   - the settings: not saved (nothing changes the octave), as host mode;
//   - the editor page's core (web/editor_core.js) against a fake FM-1, the wasm's own editor.c (the "virtual FM-1"):
//     INFO, the factory sounds, every object read, a sound edited, sent and read back, a backup restored and read
//     back as it was, a WAV made a slot (the same bytes as tools/fm1_sample_upload.py --dry-run, when Python is
//     here), uploaded and checked by the fake FM-1; the tracks live (TRACK: read, set, read back, selected), the
//     sliders' sender (a drag of 200 moves: a few frames, the last value);
//   - the page against the real FM-1's own bytes (web/fm1_frames.json, tools/fm1_editor_test.py --record), behind a
//     fake Web MIDI with a loop port (as Bitwig's "to Rack": every frame back) tried first: found, INFO, GETs, samples.
//   tests/run_host.sh && web/build_web.sh && node web/test_web.mjs
import { readFileSync, writeFileSync, mkdirSync } from "node:fs";
import { inflateSync } from "node:zlib";
import { execFileSync } from "node:child_process";
import { createHash } from "node:crypto";
import { Client, VirtualFM1, LiveSender, MidiTransport, findFM1, OBJ, parseSound, buildRecord, makeBackup, checkBackup,
         restoreBackup, makeSlot, recordOk, unpack7, hex, sampleName, SET_V1, SET_V2, settingsOf, withSettings,
         sameSettings, crc32, checkSamples, restoreSamples, EditorError, VOICE_TYPES, LINE2_WAVES, V2_SOUND_CONTROLS, PHASE_PD0,
         glideText, decayText, pctText, PITCH_TEXT, SEQ, emptyPattern, slotFromWork, slotWithPatterns, patternsFromSlot,
         patternsFromWork, slotTrack, patternFile, parsePatternFile, recallScript, stopScript, BTN_PLAY, chordTable, chordNotes, matchChord,
         recWrite, recRest, recTie, makeGrouper, pcKeyNote, noteName, emptyStep } from "./editor_core.js";

let ex, data, size, started = false, rewinding = false;
const { instance } = await WebAssembly.instantiate(readFileSync("web/dist/grimm.wasm"), {
  env: {
    web_yield() {
      if (rewinding) { ex.asyncify_stop_rewind(); rewinding = false; return; }
      const h = new Int32Array(ex.memory.buffer, data, 2);
      h[0] = data + 8;
      h[1] = data + size;
      ex.asyncify_start_unwind(data);
    },
    web_ticks: () => (performance.now() * 24000) >>> 0,   // fm1_ticks, as worklet.js
  },
});
ex = instance.exports;
ex._initialize?.();
data = ex.web_async_buf();
size = ex.web_async_size();
function run() {                                   // as worklet.js run()
  if (started) { rewinding = true; ex.asyncify_start_rewind(data); }
  started = true;
  ex.web_run();
  if (ex.asyncify_get_state() === 1) ex.asyncify_stop_unwind();
}

// firmware/src/host.c SCRIPT0: [ms, what, ...]
const BTN_FX = 2, BTN_EDIT = 6, BTN_HOME = 8, ENC_SELECT = 0, ENC_ALGORITHM = 1, ENC_KNOB1 = 2, ENC_KNOB2 = 3, ENC_KNOB3 = 4,
      ENC_KNOB4 = 5;
const CHORD1 = 1 << 7 | 1 << 11 | 1 << 14, CHORD2 = 1 << 2 | 1 << 5 | 1 << 9;
const SCRIPT = [[100, "btn", BTN_HOME], [105, "enc", ENC_SELECT, 4], [110, "enc", ENC_KNOB1, 100], [120, "btn", BTN_FX], [130, "enc", ENC_KNOB1, 40],
                [140, "enc", ENC_KNOB2, 50], [150, "enc", ENC_KNOB3, 60], [160, "enc", ENC_KNOB4, 30], [170, "btn", BTN_FX],
                [180, "enc", ENC_KNOB3, 30], [500, "keys", CHORD1], [600, "keys", 0], [700, "enc", ENC_ALGORITHM, 1],
                [710, "btn", BTN_EDIT], [720, "btn", BTN_EDIT], [730, "enc", ENC_KNOB1, 2], [740, "btn", BTN_EDIT],
                [750, "btn", BTN_HOME], [760, "btn", BTN_HOME],
                [765, "enc", ENC_SELECT, -3], [770, "enc", ENC_KNOB1, 80], [800, "keys", CHORD2],
                [900, "keys", 0], [2000, "btn", BTN_HOME], [2010, "btn", BTN_HOME], [3000, "btn", BTN_FX],
                [4000, "btn", BTN_FX], [5000, "down", BTN_FX], [5010, "keys", 1], [5020, "keys", 0], [5030, "up", BTN_FX],
                [7000, "keys", CHORD2], [7100, "keys", 0], [7200, "enc", ENC_ALGORITHM, -1], [7300, "keys", CHORD1],
                [7400, "keys", 0]];
const END = 16000, SHOTS = [2500, 3500, 6000, 15500], CPU_ROW = [29, 41];   // (ui.c: HOME's state line, CPU NOW and PEAK)

ex.web_master(1023);
ex.web_want_frames(0);                             // yield after every millisecond
const sound = [], screens = {};
let step = 0, saved = null, btnUp = 0, fail = 0, held = 0, tap = 0;
for (;;) {
  run();                                           // the app's loop body, then the next millisecond, up to the yield
  const ms = ex.web_now_ms();
  if (tap && ms > btnUp) { tap = 0; ex.web_buttons(held); }   // a tap: let go the next millisecond
  while (step < SCRIPT.length && SCRIPT[step][0] <= ms) {
    const [, what, a, b, c] = SCRIPT[step++];
    if (what === "keys") ex.web_keys(a);
    else if (what === "midi") ex.web_midi(a, b, c);
    else if (what === "enc") ex.web_enc(a, b);
    else if (what === "down") { held |= 1 << a; ex.web_buttons(held | tap); }
    else if (what === "up") { held &= ~(1 << a); ex.web_buttons(held | tap); }
    else { tap = 1 << a; ex.web_buttons(held | tap); btnUp = ms; }
  }
  const n = ex.web_avail();
  if (n) {
    ex.web_want_frames(n);
    ex.web_take();
    const l = new Float32Array(ex.memory.buffer, ex.web_out_l(), n), r = new Float32Array(ex.memory.buffer, ex.web_out_r(), n);
    for (let i = 0; i < n; i++) sound.push(Math.round(l[i] * 65536), Math.round(r[i] * 65536));
    ex.web_want_frames(0);
  }
  if (SHOTS.includes(ms)) screens[ms] = new Uint16Array(ex.memory.buffer, ex.web_fb(), 240 * 240).slice();
  if (saved === null && ex.web_settings_writes()) saved = { ms: ms - 1, len: ex.web_settings_len() };   // (in the body of ms - 1)
  if (ms >= END) break;
}
const check = (ok, what) => { console.log((ok ? "  ok    " : "  FAIL  ") + what); if (!ok) fail++; };

// the sound
const raw = readFileSync("build/host/grimm.raw"), host24 = new Int32Array(raw.buffer, raw.byteOffset, raw.length / 4);
const host = Int16Array.from(host24, (v) => Math.round(v / 128));   // (GR!MM V2: the 24-bit words, as the page plays them)
let diff = 0, first = -1, peak = 0;
for (let i = 0; i < Math.max(host.length, sound.length); i++) {
  const want = host[i] ?? 0;
  peak = Math.max(peak, Math.abs(sound[i] ?? 0));
  if (sound[i] !== want) { diff++; if (first < 0) first = i; }
}
check(sound.length === host.length, `sound: ${sound.length / 2} frames (host mode ${host.length / 2})`);
check(diff === 0 && peak > 0, `sound: sample for sample as host mode, MASTER at 1022 both (peak ${peak})` + (diff ? `: ${diff} differ, the first at frame ${first >> 1}` : ""));

// the screen
function png(path) {                               // 8-bit RGB, not interlaced (as Pillow writes it)
  const b = readFileSync(path), idat = [];
  let w = 0, h = 0;
  for (let o = 8; o < b.length;) {
    const len = b.readUInt32BE(o), type = b.toString("latin1", o + 4, o + 8);
    if (type === "IHDR") { w = b.readUInt32BE(o + 8); h = b.readUInt32BE(o + 12); if (b[o + 16] !== 8 || b[o + 17] !== 2 || b[o + 20]) throw new Error(path + ": not 8-bit RGB"); }
    if (type === "IDAT") idat.push(b.subarray(o + 8, o + 8 + len));
    o += 12 + len;
  }
  const raw = inflateSync(Buffer.concat(idat)), px = new Uint8Array(w * h * 3), st = w * 3;
  for (let y = 0; y < h; y++) {
    const f = raw[y * (st + 1)], row = raw.subarray(y * (st + 1) + 1, (y + 1) * (st + 1));
    for (let x = 0; x < st; x++) {
      const a = x >= 3 ? px[y * st + x - 3] : 0, u = y ? px[(y - 1) * st + x] : 0, c = x >= 3 && y ? px[(y - 1) * st + x - 3] : 0;
      const p = a + u - c, pa = Math.abs(p - a), pb = Math.abs(p - u), pc = Math.abs(p - c);
      const pred = [0, a, u, (a + u) >> 1, pa <= pb && pa <= pc ? a : pb <= pc ? u : c][f];
      px[y * st + x] = (row[x] + pred) & 255;
    }
  }
  return { w, px };
}
for (const t of SHOTS) {
  const { w, px } = png(`build/host/grimm-${t}.png`), fb = screens[t];
  let bad = 0;
  for (let y = 0; y < 240; y++)
    for (let x = 0; x < 240; x++) {
      if (y >= CPU_ROW[0] && y < CPU_ROW[1]) continue;
      const c = fb[y * 240 + x], o = (y * 2 * w + x * 2) * 3;   // (the PNG: the PPM scaled 2x, nearest)
      const rgb = [Math.floor((c >> 11) * 255 / 31), Math.floor((c >> 5 & 63) * 255 / 63), Math.floor((c & 31) * 255 / 31)];
      if (rgb[0] !== px[o] || rgb[1] !== px[o + 1] || rgb[2] !== px[o + 2]) bad++;
    }
  check(bad === 0, `screen at ${t} ms: pixel for pixel as build/host/grimm-${t}.png, the CPU row aside` + (bad ? ` (${bad} differ)` : ""));
}

// the settings
check(saved === null, `settings: ${saved ? `saved at ${saved.ms} ms` : "not saved"} (host mode: not saved)`);
// the editor
{
  const eq = (a, b) => a.length === b.length && a.every((x, i) => x === b[i]);
  const vf = await VirtualFM1.create(readFileSync("web/dist/grimm.wasm"));
  vf.warmUp();
  const cl = new Client(vf), info = await cl.info(), meta = vf.meta();
  check(info.proto === 2 && info.sounds === 64 && info.slotSize === 2532 && info.setSize === 170 && info.factory === 44,
        `editor: INFO (GR!MM ${info.firmware}, protocol ${info.proto}, ${info.sounds} sounds, slots ${info.slotSize} B)`);
  check(meta.factory.length === 44 && meta.engines.length === 7 && meta.fmAlg.length === 32, "editor: the page's names and tables from the firmware");
  const f2 = await cl.get(OBJ.FACT + 2, 64);
  check(eq([...f2], meta.factory[2].rec) && recordOk(f2), `editor: factory sound 3 (${meta.factory[2].name}) read as the page has it`);
  {                                                          // (V2: the sample slots are objects 5..8, read only: a PUT
    const a0 = await cl.get(OBJ.SLOT, info.slotSize);        // to one refused, slot A untouched)
    let refused = false;
    try { await cl.put(OBJ.SMP, new Uint8Array(512), info.fmt); } catch (e) { refused = e.rc === 1; }
    const a1 = await cl.get(OBJ.SLOT, info.slotSize);
    check(refused && a0.every((x, i) => x === a1[i]), "editor: a sample slot (objects 5..8) can't be written as an object");
  }
  const b0 = await makeBackup(cl, info);
  check(b0.objects.length === 69, `editor: every object read (${b0.objects.length}: settings, slots A-D, 64 sounds)`);
  {                                                          // a reply missed (a real USB link): the GET asked again
    let n = 0;
    const lossy = { request: async (f, t, q) => { const r = await vf.request(f, t, q); if (f[3] === 21 && ++n % 37 === 0) throw new EditorError("no answer from the FM-1", 97); return r; } };
    const lc = new Client(lossy), bl = await makeBackup(lc, info);
    check(lc.missed > 0 && JSON.stringify(bl.objects) === JSON.stringify(b0.objects) &&
          JSON.stringify(bl.samples) === JSON.stringify(b0.samples),
          `editor: a backup with every 37th reply lost: each asked again (${lc.missed}), the same backup`);
  }
  const s = parseSound(Uint8Array.from(meta.factory[2].rec), meta);
  Object.assign(s, { name: "TEST EDIT", chordOn: 1, voicing: 2 });
  s.k[1] = 99, s.env[0] = 20, s.lfo = [50, 30, 9, 1], s.fm.alg = 17, s.fm.fb = 5, s.fm.ops[3][2] = 77;
  const rec = buildRecord(s);
  await cl.put(OBJ.SND + 5, rec, info.fmt);
  const back = await cl.get(OBJ.SND + 5, 64);
  check(eq([...back], [...rec]) && JSON.stringify(parseSound(back, meta)) === JSON.stringify(s), "editor: a sound edited, sent, read back as edited");
  {                                                          // (GR!MM 2.0: VOICE's TYPE in byte 18: the choirs' read; set, sent, read back)
    const ty = [40, 41, 42, 43].map((i) => parseSound(Uint8Array.from(meta.factory[i].rec), meta).vtype);
    const v = parseSound(Uint8Array.from(meta.factory[40].rec), meta);
    v.vtype = 2, v.name = "TYPE TEST";
    const vr = buildRecord(v);
    await cl.put(OBJ.SND + 6, vr, info.fmt);
    const vb = parseSound(await cl.get(OBJ.SND + 6, 64), meta);
    check(ty.join() === "1,1,3,2" && vb.vtype === 2 && vb.k[1] === v.k[1] && parseSound(Uint8Array.from(meta.factory[0].rec), meta).vtype === 0,
          `editor: VOICE's TYPE read (the choirs: ${ty.join()}), set, sent, read back (${vb.vtype}); other sounds SOLO`);
  }
  const b1 = await makeBackup(cl, info);
  await cl.put(OBJ.SND + 9, rec, info.fmt);
  await cl.put(OBJ.SND + 5, new Uint8Array(64).fill(0xff), info.fmt);
  await restoreBackup(cl, checkBackup(JSON.parse(JSON.stringify(b1)), info), info);
  const b2 = await makeBackup(cl, info);
  check(b2.objects.every((o, i) => o.data === b1.objects[i].data), "editor: a backup restored reads back as it was");
  {                                                          // GR!MM V2: SETTINGS (MIDI CHANNEL, CLOCK), V1.0's backups
    const s0 = await cl.get(OBJ.SET, info.setSize), sw = withSettings(s0, { mch: 18, clk: 3 });
    await cl.put(OBJ.SET, sw, info.fmt);
    const s1 = await cl.get(OBJ.SET, info.setSize), v = settingsOf(s1);
    check(sameSettings(sw, s1) && v.mch === 18 && v.clk === 3, "editor: SETTINGS: MIDI CHANNEL CH 5 > SEL, CLOCK TRS written, read back");
    const old = Uint8Array.from([...s0.slice(0, SET_V1 - 1), 1]);   // (GR!MM 1.0's settings: 168 bytes, version 1)
    const bk = JSON.parse(JSON.stringify(b1));
    bk.objects[0] = { id: OBJ.SET, len: SET_V1, crc: crc32(old), data: Buffer.from(old).toString("base64") };
    const objs = checkBackup(bk, info);
    await restoreBackup(cl, objs.slice(0, 1), info);
    const s2 = await cl.get(OBJ.SET, info.setSize);
    check(sameSettings(old, s2) && settingsOf(s2).clk === 0, "editor: a GR!MM 1.0 backup's settings restored on V2 (CLOCK INT)");
    const v2 = Uint8Array.from([...sw.slice(0, SET_V1 - 1), 2, 3]);   // (V2's version 2: 169 bytes, CLOCK TRS, no LATCH)
    bk.objects[0] = { id: OBJ.SET, len: SET_V2, crc: crc32(v2), data: Buffer.from(v2).toString("base64") };
    await restoreBackup(cl, checkBackup(bk, info).slice(0, 1), info);
    const s3 = await cl.get(OBJ.SET, info.setSize), v3 = settingsOf(s3);
    check(sameSettings(v2, s3) && v3.clk === 3 && v3.latch === 0, "editor: a V2 version 2 backup's settings restored (CLOCK as saved, LATCH FROM SOUND)");
    const sl = withSettings(s3, { mch: 0, clk: 0, latch: 1 });
    await cl.put(OBJ.SET, sl, info.fmt);
    const s4 = await cl.get(OBJ.SET, info.setSize);
    check(sameSettings(sl, s4) && settingsOf(s4).latch === 1, "editor: SETTINGS: LATCH (SYSTEM 3) ALWAYS OFF written, read back");
    {                                                        // (V2: VELOCITY, SYSTEM 3's other: its bit beside LATCH's)
      const sv = withSettings(s4, { mch: 0, clk: 0, latch: 1, velocity: 1 });
      await cl.put(OBJ.SET, sv, info.fmt);
      const s5 = await cl.get(OBJ.SET, info.setSize), v5 = settingsOf(s5);
      check(sameSettings(sv, s5) && v5.velocity === 1 && v5.latch === 1 && settingsOf(s4).velocity === 0,
            "editor: SETTINGS: VELOCITY ON written beside LATCH, read back");
    }
    await cl.put(OBJ.SET, withSettings(s4, { mch: 0, clk: 0, latch: 0, velocity: 0 }), info.fmt);
  }
  {                                                          // GR!MM V2: FX (25): read, set live (MASTER, DRIVE, T2's filter)
    const f0 = await cl.fx();
    await cl.fxSet(16, 60), await cl.fxSet(20, 90), await cl.fxSet(21, 9), await cl.fxSet(25, 2 | 4);
    const f1 = await cl.fx();
    check(f0.length === 48 && f0[16] === 127 && f0[20] === 0 && f1[16] === 60 && f1[20] === 90 && f1[21] === 3 && f1[25] === 6,
          `editor: FX read and set live (MASTER, DRIVE FOLD, T2's filter BP POST; a value past its range: its end)`);
    await cl.fxSet(16, 127), await cl.fxSet(20, 0), await cl.fxSet(21, 0), await cl.fxSet(25, 0);
    await cl.fxSet(31, 70), await cl.fxSet(36, 33), await cl.fxSet(45, 50);   // (V2: T1's DRIVE send, T3's DLY, T2's TAPE)
    const f2 = await cl.fx();
    check(f2[31] === 70 && f2[36] === 33 && f2[45] === 50 && f0[31] === 0 && f0[45] === 0,
          `editor: FX: the tracks' sends read and set (T1 DRIVE ${f2[31]}, T3 DLY ${f2[36]}, T2 TAPE ${f2[45]})`);
    await cl.fxSet(31, 0), await cl.fxSet(36, f0[36]), await cl.fxSet(45, 0);
    // (rc9) GET only: 9, the working project as it is now (2576 B, as the autosave packs it); 10, the mix's change log
    // (entries made, u32; then 64 of 12 B: ms u32, source (2: the editor), page, field (7: TAPE), track, from, to i16)
    const wk = await cl.get(9, 2576), lg = await cl.get(10, 4 + 64 * 12), lv = new DataView(lg.buffer, lg.byteOffset);
    const n = lv.getUint32(0, true), ents = [];
    for (let i = 0; i < Math.min(n, 64); i++) {
      const o = 4 + 12 * i;
      ents.push([lg[o + 4], lg[o + 6], lg[o + 7], lv.getInt16(o + 8, true), lv.getInt16(o + 10, true)]);
    }
    const tp = ents.filter(e => e[0] === 2 && e[1] === 7 && e[2] === 1).map(e => `${e[3]}>${e[4]}`).join(" ");
    check(wk.length === 2576 && wk[38] === 100 && (wk[52 + 32] >> 3) === 0 && tp === "0>50 50>0",
          `editor: the working project read (2576 B; T1 LEVEL ${wk[38]}), the change log (T2's TAPE by the editor: ${tp})`);
  }
  const fr = 48000, n = 38400, wav = new Uint8Array(44 + n * 4), dv = new DataView(wav.buffer);   // 0.8 s, 16 bit stereo
  [..."RIFF"].forEach((c, i) => { wav[i] = c.charCodeAt(0); }), dv.setUint32(4, 36 + n * 4, true);
  [..."WAVEfmt "].forEach((c, i) => { wav[8 + i] = c.charCodeAt(0); });
  dv.setUint32(16, 16, true), dv.setUint16(20, 1, true), dv.setUint16(22, 2, true), dv.setUint32(24, fr, true);
  dv.setUint32(28, fr * 4, true), dv.setUint16(32, 4, true), dv.setUint16(34, 16, true);
  [..."data"].forEach((c, i) => { wav[36 + i] = c.charCodeAt(0); }), dv.setUint32(40, n * 4, true);
  for (let i = 0; i < n; i++) {
    const v = Math.sin(2 * Math.PI * 220 * i / fr) * 0.5 + Math.sin(2 * Math.PI * 331 * i / fr) * 0.2;
    dv.setInt16(44 + i * 4, Math.round(v * 20000), true), dv.setInt16(46 + i * 4, Math.round(v * 12000), true);
  }
  const s0 = await cl.smpInfo();                            // (power-on: an erased sample area, the factory's)
  check(s0.map((x) => `${x.state}${x.name}${x.root}`).join() === "1URCHIN60,1LEAVE60,1NIKOLAI60,1M-SOFT60",
        `editor: the factory samples in the slots that held nothing at power-on (${s0.map((x) => x.name || "-").join(", ")})`);
  const sl = makeSlot(wav.buffer, "TESTWAV", 57), zn = new DataView(sl.hdr.buffer).getUint32(36, true);
  const after = await cl.smpUpload(2, sl.hdr, sl.data);
  check(after[2].state === 1 && after[2].name === "TESTWAV" && after[2].n === zn && after[2].root === 57,
        `editor: a WAV uploaded into slot 3 and checked by the fake FM-1 (${after[2].name}, ${zn} samples, root ${after[2].root})`);
  mkdirSync("build/web", { recursive: true });
  writeFileSync("build/web/test.wav", wav);
  let py = null;
  for (const exe of ["python3", "py"]) {
    try {
      execFileSync(exe, ["tools/fm1_sample_upload.py", "3", "build/web/test.wav", "--name", "TESTWAV", "--root", "57", "--dry-run",
                         "build/web/test.bin"], { stdio: "ignore" });
      py = readFileSync("build/web/test.bin");
      break;
    } catch { /* (the next, or none) */ }
  }
  if (py) {
    const mine = new Uint8Array([...sl.hdr, ...new Uint8Array(32).fill(0xff), ...sl.data]);
    check(eq([...mine], [...py]), `editor: the WAV's slot bytes as tools/fm1_sample_upload.py makes them (${mine.length} B)`);
    let pyn = null;                                          // (NORMALISE off: the WAV's own level, the same bytes too)
    for (const exe of ["python3", "py"]) {
      try {
        execFileSync(exe, ["tools/fm1_sample_upload.py", "3", "build/web/test.wav", "--name", "TESTWAV", "--root", "57",
                           "--no-normalize", "--dry-run", "build/web/testn.bin"], { stdio: "ignore" });
        pyn = readFileSync("build/web/testn.bin");
        break;
      } catch { /* (the next, or none) */ }
    }
    const sn = makeSlot(wav.buffer, "TESTWAV", 57, false), minen = new Uint8Array([...sn.hdr, ...new Uint8Array(32).fill(0xff), ...sn.data]);
    check(pyn && eq([...minen], [...pyn]) && !eq([...sn.data], [...sl.data]),
          "editor: NORMALISE off: the WAV's own level (other bytes than normalised), as the uploader's --no-normalize");
  } else console.log("  --    editor: no Python here: the WAV's bytes not compared with tools/fm1_sample_upload.py");
  if (py) {                                                  // a long name from the file: cut as the uploader cuts it
    writeFileSync("build/web/Sample-002.wav", wav);
    for (const exe of ["python3", "py"]) {
      try { execFileSync(exe, ["tools/fm1_sample_upload.py", "3", "build/web/Sample-002.wav", "--dry-run", "build/web/test2.bin"], { stdio: "ignore" }); break; } catch { /* (the next) */ }
    }
    const nm = String.fromCharCode(...readFileSync("build/web/test2.bin").subarray(8, 16)).replace(/\0+$/, "");
    check(nm === sampleName("Sample-002") && nm === "SAMP-002", `editor: a long sample name cut as the uploader cuts it, its number kept (${nm})`);
  }
  {                                                          // GR!MM V2: the sample slots in a backup, restored
    const bs = await makeBackup(cl, info), sm = checkSamples(JSON.parse(JSON.stringify(bs)), info);
    const names = bs.samples.map((x) => x && x.name).join();
    check(names === "URCHIN,LEAVE,TESTWAV,M-SOFT" && sm.every((d, k) => d && d.length === bs.samples[k].len),
          `editor: a backup holds the four sample slots, header and data (${names})`);
    await cl.smpFactory(), await cl.smpErase(1);             // (slot 2 empty, slot 3 the factory's again)
    const done = await restoreSamples(cl, sm), back = await Promise.all([0, 1, 2, 3].map((k) => cl.smpRead(k)));
    const ok = back.every((d, k) => d && d.length === sm[k].length && d.every((x, i) => x === sm[k][i]));
    check(done.join() === "AS IT WAS,WRITTEN,WRITTEN,AS IT WAS" && ok && (await cl.smpInfo()).every((x) => x.state === 1),
          `editor: the backup's samples restored, read back the same, checked by the FM-1 (${done.join(", ")})`);
    const d2 = await restoreSamples(cl, [sm[0], sm[1], sm[2], null]), i2 = await cl.smpInfo();
    check(d2.join() === "AS IT WAS,AS IT WAS,AS IT WAS,EMPTIED" && i2[3].state === 0 && await cl.smpRead(3) === null,
          `editor: a slot empty in the backup emptied, the others not written again (${d2.join(", ")})`);
    const nb = JSON.parse(JSON.stringify(bs));
    delete nb.samples;
    check(checkSamples(nb, info) === null, "editor: an older backup (no samples): the sample slots left as they are");
    const bad = JSON.parse(JSON.stringify(bs)), bd = Buffer.from(bad.samples[2].data, "base64");
    bd[600] ^= 1, bad.samples[2].data = bd.toString("base64"), bad.samples[2].crc = crc32(bd);
    let refused = false;
    try { checkSamples(bad, info); } catch { refused = true; }
    check(refused, "editor: a damaged sample in a backup refused before anything is written");
  }
  const sf = await cl.smpFactory();
  check(sf.map((x) => `${x.state}${x.name}`).join() === "1URCHIN,1LEAVE,1NIKOLAI,1M-SOFT", `editor: RESTORE: the four factory samples (${sf.map((x) => x.name).join(", ")})`);
  // the tracks, live
  const t0 = await Promise.all([0, 1, 2, 3].map((t) => cl.track(t)));
  check(t0.every((r) => recordOk(r.rec)) && t0.every((r) => r.sel === t0[0].sel), `editor: the four tracks read (track ${t0[0].sel + 1} selected)`);
  const ls = parseSound(Uint8Array.from(meta.factory[2].rec), meta);   // FROZEN HALL (FM) onto T3, then edited
  ls.env[1] = 77, ls.lfo[1] = 42, ls.fm.ops[1][2] = 55, ls.fm.alg = 9;
  await cl.trackSet(2, buildRecord(ls));
  const lb = (await cl.track(2)).rec, lr = buildRecord(Object.assign(parseSound(lb, meta), { cat: ls.cat, num: ls.num, name: ls.name }));
  check(eq([...lr], [...buildRecord(ls)]), "editor: TRACK: a sound set live on track 3, read back as set (FM patch too)");
  check(await cl.select(1) === 1 && (await cl.track(0)).sel === 1 && await cl.select(t0[0].sel) === t0[0].sel, "editor: TRACK: a track selected, and back");
  {                                                          // (GR!MM 2.0: PHASE's LINE 2 and envelopes, a sound's bytes 18 ..)
    const ps = parseSound(Uint8Array.from(meta.factory[3].rec), meta);   // LAST LIGHT (PHASE)
    const pd0 = JSON.stringify(ps.pd);
    ps.pd.ring = 1, ps.pd.noise = 1, ps.pd.wave = 5, ps.pd.dcw = 99, ps.pd.denv = 80, ps.pd.ddec = 50, ps.pd.penv = -30, ps.pd.pdec = 64, ps.glide = 77;
    await cl.trackSet(2, buildRecord(ps));
    const pb = parseSound((await cl.track(2)).rec, meta);
    check(ps.eng === 6 && pd0 === JSON.stringify({ ring: 0, noise: 0, wave: 0, dcw: 0, denv: 0, ddec: 0, penv: 0, pdec: 0 }) && JSON.stringify(pb.pd) === JSON.stringify(ps.pd) &&
          pb.glide === 77 && pb.latch === ps.latch,
          `editor: PHASE's LINE 2 and GLIDE set live on track 3, read back (${JSON.stringify(pb.pd)}, GLIDE ${pb.glide}); a factory sound's all off`);
  }
  {                                                          // (the SOUND EDITOR's GR!MM 2.0 controls, live, then STORE IN SLOT)
    const C = V2_SOUND_CONTROLS, live = async (t, sound) => { await cl.trackSet(t, buildRecord(sound)); return parseSound((await cl.track(t)).rec, meta); };
    check(C.vtype.join() === "VOICE TYPE,0,3,0" && VOICE_TYPES.join() === "SOLO,GREGORIAN,NUNS,CATHEDRAL" &&
          C.glide.join() === "GLIDE,0,127,0" && C.line2.map((c) => c.join(":")).join() === "WAVE:0:8:0,DCW:0:127:0,RING:0:1:0,NOISE:0:1:0" &&
          C.env.map((c) => c.join(":")).join() === "DCW:0:127:0,D DEC:0:127:0,PITCH:-64:63:0,P DEC:0:127:0" &&
          LINE2_WAVES.join() === "SAME,SAW,SQR,PLS,DSIN,SPLS,RSAW,RTRI,RTRP",
          "editor: SOUND EDITOR: VOICE TYPE, GLIDE, PHASE LINE 2 (WAVE, DCW, RING, NOISE) and PHASE ENV (DCW, D DEC, PITCH, P DEC): names, ranges, defaults as the FM-1's sheet");
    check(glideText(0) === "OFF" && glideText(1) === "10 MS" && glideText(127) === "2.5 S" && decayText(0) === "10 MS" && decayText(127) === "9.7 S" &&
          pctText(0, "SAME") === "SAME" && pctText(127, "OFF") === "100%" && PITCH_TEXT(0) === "OFF" && PITCH_TEXT(-64) === "-64 ST" && PITCH_TEXT(63) === "+63 ST",
          "editor: SOUND EDITOR: the texts (GLIDE OFF, 10 MS .. 2.5 S; DECAY 10 MS .. 9.7 S; PITCH -64 .. +63 ST) as the FM-1 shows them");
    let ok = true, bad = "";
    for (let eng = 0; eng < 7; eng++) {                       // GLIDE: every source, both ends, live on the track
      const g = parseSound(Uint8Array.from(meta.factory[0].rec), meta);
      g.eng = eng, g.k = g.k.map((x, k) => Math.min(x, meta.engines[eng].max[k]));
      for (const gv of [0, 1, 64, 127]) { g.glide = gv; const b = await live(2, g); if (b.glide !== gv || b.eng !== eng || b.latch !== g.latch) ok = false, bad += ` ${eng}:${gv}`; }
    }
    check(ok, `editor: SOUND EDITOR: GLIDE live on every one of the 7 sources, read back${bad}`);
    const vo = parseSound(Uint8Array.from(meta.factory[40].rec), meta);
    ok = true;
    for (let ty = 0; ty < 4; ty++) { vo.vtype = ty; const b = await live(2, vo); if (b.vtype !== ty || b.eng !== 3) ok = false; }
    check(ok, "editor: SOUND EDITOR: VOICE TYPE SOLO, GREGORIAN, NUNS, CATHEDRAL live, read back");
    const ph = parseSound(Uint8Array.from(meta.factory[3].rec), meta);
    ok = true;
    for (const pd of [{ ring: 1, noise: 0, wave: 8, dcw: 127, denv: 127, ddec: 127, penv: 63, pdec: 127 },
                      { ring: 0, noise: 1, wave: 1, dcw: 1, denv: 1, ddec: 1, penv: -64, pdec: 1 }, { ...PHASE_PD0 }]) {
      ph.pd = { ...pd }, ph.glide = 33;
      const b = await live(2, ph);
      if (JSON.stringify(b.pd) !== JSON.stringify(pd) || b.glide !== 33) ok = false;
    }
    check(ok, "editor: SOUND EDITOR: PHASE LINE 2 and PHASE ENV, both ends of every control, live, read back");
    const st = parseSound(Uint8Array.from(meta.factory[3].rec), meta);   // STORE IN SLOT: all of them, into slot 12, read back
    Object.assign(st, { name: "ALL V2", glide: 90 });
    Object.assign(st.pd, { ring: 1, noise: 1, wave: 4, dcw: 70, denv: 50, ddec: 80, penv: -12, pdec: 40 });
    const sr = buildRecord(st);
    await cl.put(OBJ.SND + 11, sr, info.fmt);
    const sb = parseSound(await cl.get(OBJ.SND + 11, 64), meta);
    check(JSON.stringify(sb) === JSON.stringify(parseSound(sr, meta)) && sb.glide === 90 && sb.pd.penv === -12 && sb.pd.ddec === 80 && sb.name === "ALL V2",
          "editor: SOUND EDITOR: STORE IN SLOT keeps GLIDE and every PHASE setting, read back");
    await cl.put(OBJ.SND + 11, new Uint8Array(64).fill(0xff), info.fmt);
    await cl.trackSet(2, t0[2].rec);
  }
  let sent = 0, last = -1, v = 0;
  const snd = new LiveSender(async () => { sent++, last = v; await new Promise((r) => setTimeout(r, 4)); }, 2);
  for (let i = 1; i <= 200; i++) { v = i; snd.set(0); if (i % 20 === 0) await new Promise((r) => setTimeout(r, 1)); }
  await snd.running;
  check(sent <= 12 && last === 200, `editor: a slider dragged through 200 values: ${sent} frames sent, the last one last`);
  await cl.trackSet(3, buildRecord(ls), "FROZEN HALL");
  const tn = await cl.track(3);
  await cl.trackSet(3, t0[3].rec);
  check(tn.name === "FROZEN HALL" && (await cl.track(3)).name === "", `editor: TRACK: a sound's name as HOME shows it ("${tn.name}"), and none`);
  {                                                          // the SEQUENCER (patterns): into a slot, recalled, read from object 9; the sounds and mix untouched
    const run = (script) => { for (const m of script) { if ("buttons" in m) vf.ex.web_buttons(m.buttons); if ("keys" in m) vf.ex.web_keys(m.keys); vf.step(m.ms); } };
    const peak = (ms) => {                                   // the loudest sample of the next ms (the sound the virtual FM-1 makes)
      let pk = 0;
      for (let i = 0; i < ms; i++) {
        vf.run();
        const n = vf.ex.web_avail();
        if (n) {
          vf.ex.web_want_frames(n), vf.ex.web_take();
          const l = new Float32Array(vf.ex.memory.buffer, vf.ex.web_out_l(), n);
          for (let k = 0; k < n; k++) pk = Math.max(pk, Math.abs(l[k]));
          vf.ex.web_want_frames(0);
        }
      }
      return pk;
    };
    let seed = 12345;
    const rnd = (n) => { seed = (seed * 1103515245 + 12345) >>> 0; return (seed >>> 8) % n; };
    const mk = (steps, div, swing, mode, density) => {       // a pattern: notes, chances, ratchets, lengths, chords, locks
      const p = emptyPattern(steps);
      Object.assign(p, { div, swing, mode });
      p.st.forEach((s, k) => {
        if (rnd(100) < density) Object.assign(s, { note: 48 + rnd(24), chance: 20 + rnd(81), ratchet: 1 + rnd(4), len: 1 + rnd(8) });
        if (rnd(100) < 15) s.chord = 0x80 | rnd(7) << 4 | rnd(11);
        if (rnd(100) < 20) s.lmask = 1 + rnd(15), s.lv = [rnd(128), rnd(128), rnd(128), rnd(128)];
      });
      return p;
    };
    const pats = [mk(16, 4, 0, 0, 40), mk(8, 2, 30, 2, 60), mk(64, 6, 127, 3, 30), mk(1, 0, 0, 1, 100)];
    const work0 = await cl.get(9, SEQ.WORK_LEN);
    const slot0 = slotFromWork(work0, pats);                 // (an empty slot B: made of the working project's tracks)
    check(slot0.length === 2532 && slot0[0] === 1 && eq([...slot0.subarray(4, 212)], [...work0.subarray(0, 208)]) &&
          eq([...slot0.subarray(2404)], [...work0.subarray(2440, 2568)]), "sequencer: an empty slot made of the working project's tracks and FM patches");
    await cl.canWrite(OBJ.SLOT + 1, slot0, info.fmt);
    await cl.put(OBJ.SLOT + 1, slot0, info.fmt);
    const back0 = await cl.get(OBJ.SLOT + 1, 2532);
    check(eq([...back0], [...slot0]) && JSON.stringify(patternsFromSlot(back0)) === JSON.stringify(pats),
          "sequencer: four patterns (all 64 steps: note, chance, ratchet, length, chord steps, locks; steps, division, swing, play mode) written into slot B, read back, parsed the same");
    const pats2 = [mk(32, 3, 10, 1, 50), mk(4, 5, 0, 0, 90), mk(16, 7, 64, 2, 20), mk(12, 1, 5, 3, 70)];
    const slot1 = slotWithPatterns(back0, pats2);
    await cl.put(OBJ.SLOT + 1, slot1, info.fmt);
    const back1 = await cl.get(OBJ.SLOT + 1, 2532);
    const outside = (a, b) => eq([...a.subarray(0, 212)], [...b.subarray(0, 212)]) && eq([...a.subarray(2404)], [...b.subarray(2404)]);
    check(eq([...back1], [...slot1]) && outside(back1, back0) && !eq([...back1.subarray(212, 2404)], [...back0.subarray(212, 2404)]) &&
          JSON.stringify(patternsFromSlot(back1)) === JSON.stringify(pats2),
          "sequencer: new patterns over them: only bytes 212-2403 change, the tracks' sounds and mix (4-211) and the FM patches (2404-) stay as they were");
    check(eq([...(await cl.get(OBJ.SLOT, 2532)).subarray(0, 1)], [0]) && eq([...(await cl.get(OBJ.SLOT + 2, 2532)).subarray(0, 1)], [0]),
          "sequencer: the other slots (A, C) untouched");
    const sound0 = [0, 1, 2, 3].map((t) => slotTrack(back1, t));
    check(sound0.every((x, t) => x.eng === work0[t * 52] && x.k.length === 4), "sequencer: a slot's tracks' sources and knobs read for the step locks' names");
    const f = JSON.parse(JSON.stringify(patternFile(pats2, { slot: "B" })));
    let bad = "";
    try { parsePatternFile({ ...f, tracks: f.tracks.slice(0, 3) }); } catch (e) { bad = e.message; }
    check(JSON.stringify(parsePatternFile(f)) === JSON.stringify(pats2) && bad === "not a GR!MM pattern file" &&
          JSON.stringify(parsePatternFile(JSON.parse(JSON.stringify(patternFile(pats))))) === JSON.stringify(pats),
          "sequencer: a pattern file saved and loaded gives the same patterns; a damaged one is refused");
    run(recallScript(1, false));                             // recall slot B (SAVE + G3), not playing yet
    const work1 = await cl.get(9, SEQ.WORK_LEN), got = patternsFromWork(work1);
    check(JSON.stringify(got) === JSON.stringify(pats2) && eq([...work1.subarray(0, 208)].slice(0, 29), [...back1.subarray(4, 212)].slice(0, 29)),
          "sequencer: slot B recalled on the virtual FM-1, the working project (object 9) holds the patterns as written; the first track's sound as in the slot");
    const slotTracks = back1.subarray(4, 212), workTracks = work1.subarray(0, 208);
    let diff = 0;
    for (let k = 0; k < 208; k++) if (slotTracks[k] !== workTracks[k]) diff++;
    check(diff === 0, `sequencer: the recalled tracks (sound and mix bytes) equal the slot's (${diff} bytes differ)`);
    vf.step(300);
    check(peak(600) < 0.0001 && !(vf.ex.web_lit_buttons() >>> BTN_PLAY & 1), "sequencer: recalled, not playing: silent, PLAY not lit");
    run(recallScript(1, true).slice(-2));                    // PLAY
    vf.ex.web_master(760);                                   // (the virtual FM-1 is muted: MASTER up to hear it)
    const lit = vf.ex.web_lit_buttons() >>> BTN_PLAY & 1, loud = peak(5000);
    vf.ex.web_master(0);
    let refused = 0;
    try { await cl.canWrite(OBJ.SLOT + 1, slot1, info.fmt); } catch (e) { refused = e.rc; }
    check(lit === 1 && loud > 0.001 && refused === 3, `sequencer: PLAY: PLAY lit, the pattern sounds (peak ${loud.toFixed(3)}); a write is refused while it plays (rc ${refused})`);
    run(stopScript());
    vf.step(30000);                                          // (the tails end, SPACE's: the FM-1 may write again)
    let ok = false;
    try { await cl.canWrite(OBJ.SLOT + 1, slot1, info.fmt); ok = true; } catch (e) { /* still sounding */ }
    check(!(vf.ex.web_lit_buttons() >>> BTN_PLAY & 1) && ok, "sequencer: stopped: PLAY dark, silent, a write is taken again");
    await cl.put(OBJ.SLOT + 1, new Uint8Array(2532), info.fmt);   // (slot B empty again, as at power-on)
  }

// STEP RECORD (the SEQUENCER's REC): the record logic of editor_core.js
{
  const table = chordTable(), want = readFileSync("tests/data/chord_table.sha256", "utf8").trim();
  check(createHash("sha256").update(table).digest("hex") === want && table.split("\n").length === 11 * 5 * 128 + 1,
        "step record: the chord table (11 CHORD TYPES x 5 VOICINGS x 128 roots) is the firmware's own (scale.c chord_notes_tv: tests/chord_table.c, same hash)");
  const P = (steps = 4) => emptyPattern(steps);
  let p = P(4), r = recWrite(p, 0, [60]);
  check(r.cursor === 1 && r.kind === "note" && p.st[0].note === 60 && p.st[0].chord === 0, "step record: a note goes into the step and the cursor moves on");
  r = recWrite(p, r.cursor, [62]), r = recWrite(p, r.cursor, [64]);
  check(p.st[1].note === 62 && p.st[2].note === 64 && r.cursor === 3, "step record: the next notes go into the next steps");
  r = recWrite(p, 3, [65]);
  check(p.st[3].note === 65 && r.cursor === 0, "step record: at the last step (STEPS 4) the cursor wraps to the first");
  r = recWrite(p, 0, [67]);
  check(p.st[0].note === 67 && r.cursor === 1, "step record: after the wrap the first step is written over");
  p = P(16), p.st[5] = { ...emptyStep(), note: 70, chance: 50, ratchet: 3, len: 4, lmask: 5, lv: [1, 2, 3, 4] };
  recWrite(p, 5, [72]);
  check(p.st[5].note === 72 && p.st[5].chance === 50 && p.st[5].ratchet === 3 && p.st[5].len === 4 && p.st[5].lmask === 5, "step record: a note replaces only the step's NOTE (chance, ratchet, length, locks stay)");
  r = recWrite(p, 5, [64, 60]);
  check(p.st[5].note === 60 && r.kind === "pair" && p.st[5].chord === 0, "step record: two keys: the lowest note");
  p = P(3), p.st[0].note = 60, p.st[1].note = 62, p.st[1].chord = 0x81;
  r = recRest(p, 1);
  check(p.st[1].note === 0 && p.st[1].chord === 0 && r.cursor === 2 && recRest(p, 2).cursor === 0, "step record: REST makes the step OFF, clears a chord, moves on, wraps");
  p = P(4), recWrite(p, 0, [60]);
  let t = recTie(p, 1);
  check(t && t.tied === 0 && t.len === 2 && p.st[0].len === 2 && t.cursor === 1, "step record: TIE makes the step before one longer, the cursor stays");
  t = recTie(p, 1);
  check(t.len === 3 && p.st[0].len === 3 && recTie(p, 2) === null, "step record: TIE again: longer again; after a step with no note: nothing to tie");
  p.st[0].len = 64;
  check(recTie(p, 1).len === 64, "step record: a tie stops at 64 steps");
  p = P(8);
  r = recWrite(p, 0, [60, 63, 67]);
  check(r.kind === "chord" && p.st[0].note === 60 && p.st[0].chord === 0x81, `step record: C, D#, G together: a chord step, MIN CLOSE on C (byte 0x${p.st[0].chord.toString(16)})`);
  r = recWrite(p, 1, [64, 60, 67]);
  check(r.kind === "chord" && p.st[1].note === 60 && p.st[1].chord === 0x82, "step record: the same keys in any order: MAJ CLOSE on C");
  r = recWrite(p, 2, [60, 67, 76]);
  check(r.kind === "chord" && p.st[2].note === 60 && p.st[2].chord === 0x92, "step record: C, G, E an octave up: MAJ OPEN on C");
  r = recWrite(p, 3, [48, 64, 67]);
  check(r.kind === "chord" && p.st[3].note === 60 && p.st[3].chord === 0xc2, "step record: a DROP voicing (the root an octave down): the step's note is the root, not the lowest key");
  r = recWrite(p, 4, [60, 64, 67, 71]);
  check(r.kind === "chord" && p.st[4].chord === 0x87, "step record: four keys: MAJ7");
  r = recWrite(p, 5, [60, 65, 67]);
  check(r.kind === "chord" && p.st[5].note === 60 && p.st[5].chord === 0x84, "step record: C, F, G: SUS4 on C (the lowest key as the root)");
  r = recWrite(p, 6, [60, 61, 62]);
  check(r.kind === "nomatch" && p.st[6].note === 60 && p.st[6].chord === 0 && r.cursor === 7, "step record: a chord not on the FM-1: the lowest note is written, flagged (CHORD NOT ON THE FM-1)");
  check(matchChord([60, 62, 66, 69, 72, 75]) === null && matchChord([60, 64]) === null, "step record: six odd keys, or two keys: no chord");
  const all = [];                                            // every chord the FM-1 builds (but SCALE's), played as its notes, is found and plays the same
  let bad = 0;
  for (let vo = 0; vo < 5; vo++) for (let ty = 1; ty <= 10; ty++) for (const root of [24, 48, 60, 72, 100]) {
    const ns = chordNotes(ty, vo, root);
    if (new Set(ns).size < 3) continue;
    const m = matchChord(ns);
    all.push(1);
    if (!m || [...new Set(chordNotes(m.type, m.voicing, m.note))].sort((a, b) => a - b).join() !== [...new Set(ns)].sort((a, b) => a - b).join()) bad++;
  }
  check(all.length > 200 && bad === 0, `step record: ${all.length} chords of the FM-1 (types, voicings, roots) pressed as keys: each found, and the chord found plays the same notes (${bad} differ)`);
  p = P(8), recWrite(p, 0, [60, 63, 67]);
  const sl = slotWithPatterns(slotFromWork(await cl.get(9, SEQ.WORK_LEN), [p, P(), P(), P()]), [p, P(), P(), P()]);
  check(patternsFromSlot(sl)[0].st[0].chord === 0x81 && patternsFromSlot(sl)[0].st[0].note === 60, "step record: a recorded chord step is kept in the slot's bytes");
  const got = [];                                            // keys within 50 ms are one group
  const gr = makeGrouper((n) => got.push(n.join()), 50);
  const coreSrc = readFileSync("web/editor_core.js", "utf8");
  check(!/timers = \{ set: setTimeout \}/.test(coreSrc) && /timers = \{ set: \(f, t\) => setTimeout\(f, t\) \}/.test(coreSrc),
        "step record: the default timer is a plain call of setTimeout (as a method a browser's setTimeout is an Illegal invocation: found in a real browser)");
  gr.add(60), await new Promise((r2) => setTimeout(r2, 15)), gr.add(63), await new Promise((r2) => setTimeout(r2, 15)), gr.add(67);
  await new Promise((r2) => setTimeout(r2, 120));
  gr.add(72);
  await new Promise((r2) => setTimeout(r2, 120));
  check(got.join(" | ") === "60,63,67 | 72", `step record: keys within 50 ms are one group, a later key another (${got.join(" | ")})`);
  check(pcKeyNote("a", 4) === 60 && pcKeyNote("w", 4) === 61 && pcKeyNote("j", 4) === 71 && pcKeyNote("k", 4) === 72 && pcKeyNote("a", 3) === 48 &&
        pcKeyNote("q", 4) === -1 && pcKeyNote("k", 10) === -1 && "awsedftgyhujk".split("").map((c) => pcKeyNote(c, 4)).join() === "60,61,62,63,64,65,66,67,68,69,70,71,72",
        "step record: the computer's keys A W S E D F T G Y H U J K are one octave (A = C4 at OCT 4), none out of range");
  check(noteName(60) === "C4" && noteName(63) === "D#4" && noteName(48) === "C3" && noteName(1) === "C#-1", "step record: notes are named C4, D#4, C3");
}
}
// the page against the FM-1's own bytes, behind a fake Web MIDI: a GM synth (silent), a loop port, then the FM-1
{
  const rec = JSON.parse(readFileSync("web/fm1_frames.json", "utf8")), replies = new Map(rec.frames.map((f) => [f.tx, f.rx]));
  const bytes = (h) => Uint8Array.from(h.split(" ").map((x) => parseInt(x, 16)));
  const later = (p, d) => setTimeout(() => p.onmidimessage && p.onmidimessage({ data: d }), 1);
  const midi = (fm) => {                                     // fm: the FM-1's port names (out, in)
    const loopIn = { name: "Bitwig to Rack 0", onmidimessage: null }, fmIn = { name: fm[1], onmidimessage: null };
    const outs = [{ name: "Microsoft GS Wavetable Synth 0", send() {} }, { name: "Bitwig to Rack 1", send(d) { later(loopIn, Uint8Array.from(d)); } },
                  { name: fm[0], send(d) { const r = replies.get(hex(d)); if (r) later(fmIn, bytes(r)); } }];
    return { outputs: new Map(outs.map((o) => [o.name, o])), inputs: new Map([[loopIn.name, loopIn], [fmIn.name, fmIn]]) };
  };
  const log = [], p = await findFM1(midi(["Felucca 2", "Felucca 1"]), (dir, d, name) => log.push(`${dir} ${name}`));
  check(p && p.output.name === "Felucca 2" && p.input.name === "Felucca 1", `editor: the FM-1 found by its name, "Felucca" (${log.length} frames in the log)`);
  log.length = 0;                                            // (named otherwise: the loop port tried first, its echo not taken)
  const q = await findFM1(midi(["USB MIDI Device 2", "USB MIDI Device 1"]), (dir, d, name) => log.push(`${dir} ${name}`));
  check(q && q.output.name === "USB MIDI Device 2" && log.includes("in Bitwig to Rack 0"),
        `editor: the FM-1 found behind a loop port that sends INFO back (26b's "PROTOCOL UNDEFINED"; ${log.length} frames in the log)`);
  const cl = new Client(new MidiTransport(p.input, p.output)), info = await cl.info();
  check(info.proto === rec.proto && info.firmware === rec.firmware && info.sounds === 64 && info.setSize === 168,
        `editor: INFO as the FM-1 sent it (GR!MM ${info.firmware}, protocol ${info.proto})`);
  const set = await cl.get(OBJ.SET, 168), s1 = await cl.get(OBJ.SND, 64), f1 = await cl.get(OBJ.FACT, 64);
  const want = (k) => unpack7([...bytes(rec.frames[k].rx)].slice(14, -1));
  check(eq([...set], [...want(1)]) && eq([...s1], [...want(2)]) && recordOk(f1), "editor: the settings, sound 1 and factory sound 1 as the FM-1 sent them");
  const smp = await cl.smpInfo();
  check(smp.length === 4 && smp[0].state >= 0, `editor: the samples' INFO as the FM-1 sent it (${smp.map((s) => s.state < 0 ? "?" : s.name || "-").join(", ")})`);
  function eq(a, b) { return a.length === b.length && a.every((x, i) => x === b[i]); }
}

// the editor page's texts and looks (web/editor.html, editor.js, LICENSING.md): naming, credits, BACKUP, names whole, the hub's look
{
  const html = readFileSync("web/editor.html", "utf8"), js = readFileSync("web/editor.js", "utf8"), lic = readFileSync("LICENSING.md", "utf8");
  const vf = await VirtualFM1.create(readFileSync("web/dist/grimm.wasm")), meta = vf.meta(), info = await new Client(vf).info();
  mkdirSync("build/web", { recursive: true });                // (the page's script parses: it is a module, so a copy as .mjs)
  writeFileSync("build/web/editor_check.mjs", js);
  let syntax = "";
  try { execFileSync(process.execPath, ["--check", "build/web/editor_check.mjs"], { stdio: "pipe" }); } catch (e) { syntax = String(e.stderr).split("\n").slice(0, 4).join(" "); }
  check(!syntax, "editor page: web/editor.js parses" + (syntax ? ` (${syntax})` : ""));
  check(!/demo/i.test(html) && !/demo/i.test(js), 'editor page: the word "demo" is nowhere (page, script)');
  check(/<a class="lnk px" href="index\.html">WEB EMULATOR<\/a>/.test(html) && />Try the GR!MM web emulator<\/a><\/b>/.test(html),
        'editor page: the header link says WEB EMULATOR (to index.html), the footer "Try the GR!MM web emulator"');
  const bk = html.slice(html.indexOf('id="s-backup"'), html.indexOf("</section>", html.indexOf('id="s-backup"')));
  check(/four sample slots/.test(bk) && !/Samples are not in it/i.test(html) && !/keep their WAV files/.test(bk) && !/cannot send its samples/.test(html),
        'editor page: BACKUP says it holds the four samples; the old "keep their WAV files" is gone');
  check(/choir recordings: <a [^>]*>VocalSet<\/a> \(Wilkins et al\., 2018\), <a class="lnk" href="https:\/\/creativecommons\.org\/licenses\/by\/4\.0\/">CC BY 4\.0<\/a>/.test(html) &&
        /choir recordings: VocalSet \(Wilkins et al\., 2018\), CC BY 4\.0/.test(lic) && lic.includes("https://creativecommons.org/licenses/by/4.0/") &&
        ["Press Start 2P", "Terminus", "CC0 1.0", "Emscripten", "Dinghy", "Felucca", "SLOOP"].every((c) => html.includes(c)),
        "editor page and LICENSING.md: the VocalSet credit (CC BY 4.0, its link) added, the old credits still there");
  check(info.firmware === "2.0" && meta.version === "2.0" && info.factory === 44 && meta.factory.length === 44 && /EDITOR 2\.0/.test(html) &&
        /"FIRMWARE", "GR!MM " \+ i\.firmware/.test(js) && /\["FACTORY SOUNDS", i\.factory\]/.test(js),
        `editor page: CONNECT shows GR!MM ${info.firmware} and ${info.factory} factory sounds; the header EDITOR 2.0`);
  const names = meta.factory.map((x) => x.name), choirs = ["GREGORIAN", "PLAINCHANT", "CATHEDRAL CHOIR", "NUNS"];
  const srcs = meta.factory.map((x) => meta.engines[parseSound(Uint8Array.from(x.rec), meta).eng].name);
  check(names.length === 44 && new Set(names).size === 44 && choirs.every((c) => names.some((n) => n.toUpperCase() === c)) &&
        names.some((n) => n.length > 12) && srcs.every(Boolean) && /slotBox\(f, slotLabel/.test(js) && /el\("small", \{ cls: "src" \}/.test(js) &&
        !/\.slot\s*\{[^}]*(text-overflow|white-space:\s*nowrap)/.test(html) && /\.slot \.src \{ display: block/.test(html),
        `editor page: all 44 factory sounds by their full names (longest: ${names.reduce((a, n) => (n.length > a.length ? n : a), "")}), the source a tag on a second line, nothing cut`);
  check(/id="smp-norm" checked> NORMALISE/.test(html) && /\$\("smp-norm"\)\.checked/.test(js) && /GR!MM 2\.0's encoder no longer clicks/.test(html),
        "editor page: SAMPLES has the NORMALISE switch (on by default), its help text for V2.0");
  check(/p\.note, footer\.note \{ color: var\(--n\)/.test(html) && /--n: #8f887b/.test(html) && /--m: #5e5b54/.test(html) &&
        /class="tag px" href="hub\.html"/.test(html) && /<div class="drip" role="presentation">/.test(html) &&
        /outline: 3px solid var\(--f\)/.test(html) && /--f: #ff6a3d/.test(html) && /<a href="#editor" data-s="editor">/.test(html) &&
        /@media \(max-width: 599px\)[^]*nav \{[^}]*flex-direction: row; overflow-x: auto/.test(html),
        "editor page: help text #8f887b, the GR!MM tag to hub.html, the drip, 3px #ff6a3d focus, the menu one scrolling row under 600 px");
  check(/<a href="#sequencer" data-s="sequencer">SEQUENCER<\/a>/.test(html) && ["sq-slot", "sq-import", "sq-now", "sq-send", "sq-preview", "sq-stop", "sq-save", "sq-load", "sq-clear", "sq-grid", "sq-step", "sq-trk"].every((i) => html.includes(`id="${i}"`)) &&
        /repeat\(8, minmax\(0, 1fr\)\)/.test(html) && /@media \(min-width: 900px\) \{ \.sqgrid/.test(html) && /canWrite\(OBJ\.SLOT \+ k, next/.test(js) &&
        /grimm-slot-\$\{L\}-backup/.test(js) && /slotWithPatterns\(cur, sq\.pats\)/.test(js) && /type: "reply"/.test(readFileSync("web/worklet.js", "utf8")),
        "editor page: the SEQUENCER section (grid 8 wide on a phone, 16 on a computer; IMPORT, SEND with a probe and a backup first, PREVIEW, files)");
  check(["sq-rec", "sq-rest", "sq-tie", "sq-mon", "sq-octd", "sq-octu", "sq-midi", "sq-midi-scan", "sq-recst", "sq-recmsg"].every((i) => html.includes(`id="${i}"`)) &&
        /min-height: 44px/.test(html) && /\.sqgrid\.rec \.sqs\.sel \{[^}]*outline: 3px solid var\(--f\)/.test(html) &&
        /FM1_PORT = \/felucca\|fm-\?1\|m-\?vave\|gr!\?mm\/i/.test(js) && /localStorage\.getItem\(SQ_MIDI_KEY\)/.test(js) && /try \{ return localStorage/.test(js) && /try \{ localStorage\.setItem/.test(js) &&
        /requestMIDIAccess\(\{ sysex: false \}\)/.test(js) && /CHORD NOT ON THE FM-1/.test(js) && /c === 0xb0 && d1 === 64/.test(js) &&
        /sqTyping\(\)/.test(js) && /\(sq\.rec && sqOn\(\)\)/.test(js) && /k === "Backspace"/.test(js) && /k === " "/.test(js) && /k === "z"/.test(js) && /k === "x"/.test(js) &&
        /b\.append\(s\.note \? noteName\(s\.note\) : "OFF"\)/.test(js) && /\(v\) => \(v \? noteName\(v\) : "OFF"\)/.test(js),
        "editor page: STEP RECORD (REC, REST, TIE, MONITOR, MIDI IN without the FM-1's port, kept in localStorage in try/catch, the pedal, the computer's keys, notes as names, the cursor outlined, 44 px buttons)");
  check(/PHASE LINE 2/.test(js) && /PHASE ENV/.test(js) && /V2_SOUND_CONTROLS/.test(js) && /VOICE_TYPES/.test(js) && /glideText/.test(js) && /decayText/.test(js) &&
        /b-store/.test(js),
        "editor page: the SOUND EDITOR draws VOICE TYPE, GLIDE, PHASE LINE 2 and PHASE ENV from the sheet's names");
}

console.log(fail ? `test_web: ${fail} failed` : "test_web: all ok");
process.exit(fail ? 1 : 0);
