// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GR!MM
// The browser build against host mode: runs web/dist/grimm.wasm as web/worklet.js does (Asyncify), one millisecond at
// a time, with tests/host.c's script, and compares with what tests/run_host.sh made:
//   - the sound, sample for sample (GR!MM V2: the app applies MASTER, fm1_master: tests/run_host.sh renders at the
//     knob's 1022, as here);
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
import { Client, VirtualFM1, LiveSender, MidiTransport, findFM1, OBJ, parseSound, buildRecord, makeBackup, checkBackup,
         restoreBackup, makeSlot, recordOk, unpack7, hex, sampleName, SET_V1, SET_V2, settingsOf, withSettings,
         sameSettings, crc32, checkSamples, restoreSamples, EditorError } from "./editor_core.js";

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

// tests/host.c SCRIPT0: [ms, what, ...]
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
const wav = readFileSync("build/host/grimm.wav"), host = new Int16Array(wav.buffer, wav.byteOffset + 44, (wav.length - 44) / 2);
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
  let sent = 0, last = -1, v = 0;
  const snd = new LiveSender(async () => { sent++, last = v; await new Promise((r) => setTimeout(r, 4)); }, 2);
  for (let i = 1; i <= 200; i++) { v = i; snd.set(0); if (i % 20 === 0) await new Promise((r) => setTimeout(r, 1)); }
  await snd.running;
  check(sent <= 12 && last === 200, `editor: a slider dragged through 200 values: ${sent} frames sent, the last one last`);
  await cl.trackSet(3, buildRecord(ls), "FROZEN HALL");
  const tn = await cl.track(3);
  await cl.trackSet(3, t0[3].rec);
  check(tn.name === "FROZEN HALL" && (await cl.track(3)).name === "", `editor: TRACK: a sound's name as HOME shows it ("${tn.name}"), and none`);
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

console.log(fail ? `test_web: ${fail} failed` : "test_web: all ok");
process.exit(fail ? 1 : 0);
