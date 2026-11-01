// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GR!MM
// GR!MM's editor page (web/editor.html). As SLOOP's editor (isod89/sloop-fm1, web/, GPL-3.0) works on the sound that
// plays: the SOUND EDITOR shows the four tracks' sounds (T1..T4) and every move of a control reaches the track at once,
// on the FM-1 (editor.c's TRACK, live, no flash) and in GR!MM's own engine here (grimm.wasm in an AudioWorklet,
// worklet.js: its screen and its keys). STORE IN SLOT saves it into a user slot (flash: only by that button, the FM-1
// stopped and silent). SOUNDS lists the user's and the factory sounds (LOAD TO T1..T4), SAMPLES the sample slots, BACKUP
// everything. The protocol, the records, the WAV conversion: editor_core.js. Without an FM-1 the same, with the
// "virtual FM-1" (grimm.wasm here, editor_core.js's VirtualFM1) in its place.
import { Client, MidiTransport, VirtualFM1, LiveSender, findFM1, HEAD, CMD, OBJ, PROTOS, EditorError, emptyRecord, isEmpty,
         recordOk, parseSound, buildRecord, soundTitle, makeBackup, checkBackup, restoreBackup, makeSlot, noteFromName,
         sampleName, noteName, pack7, hex, SET_V1, SET_V2, MCH_NAMES, CLOCK_NAMES, settingsOf, withSettings,
         sameSettings, FX_PAGES, FT_NAMES, checkSamples, restoreSamples, VOICE_TYPES, LINE2_WAVES, V2_SOUND_CONTROLS, PHASE_PD0,
         glideText, decayText, pctText, PITCH_TEXT, SEQ, DIV_NAMES, MODE_NAMES, emptyPattern, emptyStep, patternsFromSlot,
         patternsFromWork, slotWithPatterns, slotFromWork, slotTrack, patternFile, parsePatternFile, recallScript, stopScript,
         BTN_PLAY, b64, recWrite, recRest, recTie, makeGrouper, pcKeyNote } from "./editor_core.js";

const $ = (id) => document.getElementById(id);
function el(tag, attrs, parent, text) {
  const e = document.createElement(tag);
  for (const [k, v] of Object.entries(attrs || {})) {
    if (k === "cls") e.className = v;
    else if (k.startsWith("on")) e.addEventListener(k.slice(2), v);
    else e.setAttribute(k, v);
  }
  if (text !== undefined) e.textContent = text;
  if (parent) parent.appendChild(e);
  return e;
}

let wasm = null, vf = null, vfc = null, meta = null;        // grimm.wasm; its instance here (texts, the virtual FM-1)
let conn = null;                                             // { client, info, kind: "fm1" | "virtual", transport, live }
let busy = false;
let setBytes = null;                                         // the settings as last read from the FM-1 (SETTINGS)
let fxVals = null;                                           // the project's effects as last read (FX: 48 values)
const user = Array.from({ length: 64 }, () => emptyRecord()), dirty = new Array(64).fill(false);
let pick = { fact: true, i: 0 }, readAt = "";                // the sound chosen under SOUNDS
const tracks = [null, null, null, null], loaded = ["", "", "", ""], from = ["", "", "", ""];   // T1..T4: the sound,
let selT = 0, touchedAt = 0;                                 // as loaded (REVERT), where it came from; the last edit
const label = ["", "", "", ""], labelEng = [0, 0, 0, 0];   // a track's sound's name as the FM-1's HOME shows it (the
                                                             // factory or user sound loaded, the editor's), for its source

// ------------------------------------------------------------------ messages, progress, busy, the MIDI log
function say(text, kind = "") { const m = $("msg"); m.textContent = text; m.className = "px " + kind; }
function progress(done, total) {
  const p = $("prog");
  p.style.visibility = total ? "visible" : "hidden";
  p.firstChild.style.width = total ? `${Math.round(done / total * 100)}%` : "0";
}
async function job(what, fn) {                               // one write or read at a time, its result said
  if (busy) { say("WAIT: SOMETHING IS STILL RUNNING", "err"); return; }
  busy = true;
  refreshButtons();
  try {
    const r = await fn();
    if (r !== false) say(typeof r === "string" ? r : `${what}: DONE`, "ok");
  } catch (e) {
    say(`${what}: ${e instanceof EditorError ? e.message : (e && e.message) || e}`.toUpperCase(), "err");
  } finally {
    busy = false;
    progress(0, 0);
    refreshButtons();
  }
}
const errText = (e) => (e instanceof EditorError ? e.message : (e && e.message) || String(e)).toUpperCase();
function download(name, text) {
  const a = el("a", { href: URL.createObjectURL(new Blob([text], { type: "application/json" })), download: name });
  a.click();
  setTimeout(() => URL.revokeObjectURL(a.href), 5000);
}
const stamp = () => new Date().toISOString().replace(/[-:]/g, "").replace("T", "-").slice(0, 15);
const logLines = [];
let logDrawn = false;
function midiLog(dir, bytes, port) {                         // every SysEx frame sent and received (the live ones not)
  const t = new Date().toTimeString().slice(0, 8);
  logLines.push(`${t} ${dir === "out" ? "->" : "<-"} ${port}: ${hex(bytes)}`);
  if (logLines.length > 400) logLines.splice(0, logLines.length - 400);
  if (!logDrawn && !$("log").hidden) { logDrawn = true; requestAnimationFrame(drawLog); }
}
function drawLog() { logDrawn = false; const l = $("log"); l.textContent = logLines.join("\n") || "(NOTHING YET)"; l.scrollTop = l.scrollHeight; }
$("b-log").onclick = () => { const l = $("log"); l.hidden = !l.hidden; $("b-log").classList.toggle("on", !l.hidden); if (!l.hidden) drawLog(); };
$("b-logclear").onclick = () => { logLines.length = 0; drawLog(); };

// ------------------------------------------------------------------ sections
function show(s) {
  document.querySelectorAll("nav a").forEach((a) => a.classList.toggle("on", a.dataset.s === s));
  document.querySelectorAll("section").forEach((x) => x.classList.toggle("on", x.id === "s-" + s));
  if (s === "samples" && conn) sampleInfo();
  if (s === "sequencer") drawSeq();
}
document.querySelectorAll("nav a").forEach((a) => a.addEventListener("click", (e) => { e.preventDefault(); show(a.dataset.s); history.replaceState(null, "", "#" + a.dataset.s); }));

// ------------------------------------------------------------------ the engine: GR!MM in an AudioWorklet (sound, screen)
let ac = null, node = null, engReady = null;
const scr = $("screen").getContext("2d"), img = scr.createImageData(240, 240);
function screenText(lines) {
  scr.fillStyle = "#0b0b0a", scr.fillRect(0, 0, 240, 240);
  scr.fillStyle = "#b8b4a9", scr.font = '8px "Press Start 2P", monospace', scr.textAlign = "center";
  lines.forEach((l, i) => scr.fillText(l, 120, 108 + i * 14));
}
function drawScreen(fb) {                                    // RGB565 (panel.js)
  const d = img.data;
  for (let i = 0; i < 240 * 240; i++) {
    const p = fb[i];
    d[i * 4] = ((p >> 11) & 31) * 255 / 31, d[i * 4 + 1] = ((p >> 5) & 63) * 255 / 63, d[i * 4 + 2] = (p & 31) * 255 / 31;
    d[i * 4 + 3] = 255;
  }
  scr.putImageData(img, 0, 0);
}
function engSend(frame) { if (node) node.port.postMessage({ type: "sysex", data: frame }); }   // (no F0 / F7)
function startEngine() {                                     // at once; the browser lets it sound after a click or a key
  if (engReady) { if (ac && ac.state !== "running") ac.resume(); return engReady; }
  engReady = (async () => {
    ac = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
    await ac.audioWorklet.addModule("worklet.js");
    node = new AudioWorkletNode(ac, "grimm", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
    node.connect(ac.destination);
    await new Promise((res, rej) => {
      node.port.onmessage = (e) => {
        const m = e.data;
        if (m.type === "ready") res();
        else if (m.type === "error") rej(new Error(m.message));
        else if (m.type === "reply") engReply(m.data);
        else if (m.type === "frame") {
          if (m.leds) engLeds = m.leds, sqPlayState();
          if (m.fb) drawScreen(m.fb);
        }
      };
      node.port.postMessage({ type: "load", wasm: wasm.slice(0), master: 760 });
    });
    for (let t = 0; t < 4; t++) engTrack(t);                 // (the page's four sounds, the page's track)
    engSend([...HEAD, CMD.TRACK, 2, selT]);
    await ac.resume();
  })().catch((e) => { screenText(["NO SOUND HERE:", String(e.message || e).slice(0, 26).toUpperCase()]); });
  return engReady;
}
addEventListener("pointerdown", startEngine, true);
addEventListener("keydown", startEngine, true);

// ------------------------------------------------------------------ connect
function setConn(c) {
  if (conn && conn.transport.close) conn.transport.close();
  conn = c;
  setBytes = null, fxVals = null;                            // (another FM-1, or none: read its settings, FX again)
  if ($("fx").childElementCount) drawFx();
  if ($("set-mch").options.length) drawSettings();
  const s = $("conn");
  s.textContent = !c ? "NOT CONNECTED" : c.kind === "virtual" ? `VIRTUAL FM-1 · GR!MM ${c.info.firmware}` : `FM-1 · GR!MM ${c.info.firmware}`;
  s.classList.toggle("on", !!c);
  const t = $("info");
  t.textContent = "";
  if (!c) {                                                  // (not connected: this page's own GR!MM, which it plays and edits with)
    [["THIS PAGE'S GR!MM", `GR!MM ${meta.version}`], ["FACTORY SOUNDS", meta.factory.length]]
      .forEach(([k, v]) => { const r = el("tr", {}, t); el("td", {}, r, k); el("td", {}, r, String(v)); });
  } else {
    const i = c.info;
    [["CONNECTED TO", c.kind === "virtual" ? "THE VIRTUAL FM-1 (THIS PAGE)" : `THE FM-1 OVER USB (${c.transport.output.name})`],
     ["FIRMWARE", "GR!MM " + i.firmware], ["EDITOR PROTOCOL", i.proto + (c.live ? " (LIVE EDITING)" : " (NO LIVE EDITING: INSTALL 26B2)")],
     ["USER SOUND SLOTS", i.sounds], ["FACTORY SOUNDS", i.factory], ["SAMPLE SLOTS IN USE", `${i.samplesUsed} OF ${i.sampleSlots}`]]
      .forEach(([k, v]) => { const r = el("tr", {}, t); el("td", {}, r, k); el("td", {}, r, String(v)); });
  }
  drawTracks();
  refreshButtons();
}
async function connectFM1() {
  if (!navigator.requestMIDIAccess) { say("THIS BROWSER HAS NO WEB MIDI: USE CHROME OR EDGE", "err"); return; }
  await job("CONNECT", async () => {
    say("ASKING FOR MIDI ACCESS…");
    const access = await navigator.requestMIDIAccess({ sysex: true });
    say("LOOKING FOR GR!MM ON THE MIDI PORTS…");
    const p = await findFM1(access, midiLog);
    if (!p) throw new EditorError("NO FM-1 WITH GR!MM FOUND (USB DATA CABLE? ANOTHER APP ON ITS MIDI PORT?) SEE THE MIDI LOG", 0);
    const transport = new MidiTransport(p.input, p.output, midiLog), client = new Client(transport), info = await client.info();
    if (!PROTOS.includes(info.proto)) throw new EditorError(`THE FM-1 SPEAKS EDITOR PROTOCOL ${info.proto}; THIS PAGE KNOWS ${PROTOS.join(" AND ")}`, 0);
    setConn({ client, info, kind: "fm1", transport, live: info.proto >= 2 });
    return afterConnect(true, `ON ${p.output.name}`);
  });
}
async function tryVirtual() {
  await job("CONNECT", async () => {
    say("STARTING THE VIRTUAL FM-1…");
    await new Promise((r) => setTimeout(r, 30));
    vf.warmUp();
    const info = await vfc.info();
    setConn({ client: vfc, info, kind: "virtual", transport: vf, live: true });
    return afterConnect(false, "(THE VIRTUAL FM-1: GR!MM'S OWN CODE IN THIS PAGE, NOTHING IS KEPT)");
  });
}
// connected: the FM-1's four tracks into the page (the virtual one takes the page's), then its user slots
async function afterConnect(read, where) {
  if (conn.live && read) {
    say("READING THE FOUR TRACKS…");
    let sel = 0;
    for (let t = 0; t < 4; t++) {
      const r = await conn.client.track(t);
      tracks[t] = null;                                      // (another sound: not the page's name for it)
      sel = r.sel, tracks[t] = fromDevice(r.rec, t), loaded[t] = JSON.stringify(tracks[t]), from[t] = "ON THE FM-1";
      label[t] = r.name, labelEng[t] = tracks[t].eng;
      engTrack(t);
    }
    selT = sel, engSend([...HEAD, CMD.TRACK, 2, selT]);
  } else if (conn.live) {
    for (let t = 0; t < 4; t++) await conn.client.trackSet(t, buildRecord(tracks[t]), homeName(t));
    await conn.client.select(selT);
  }
  await readSlots();
  drawTracks(), drawEditor(), drawSlots();
  show("editor");
  history.replaceState(null, "", "#editor");
  return `CONNECTED ${where}: GR!MM ${conn.info.firmware}` + (conn.live ? `, TRACK ${selT + 1} IN THE EDITOR (LIVE)` :
    ": ITS GR!MM IS OLDER THAN 26B2, SO THE SLIDERS PLAY ONLY HERE; SOUNDS, SAMPLES, BACKUP WORK");
}
$("b-connect").onclick = connectFM1;
$("b-try").onclick = tryVirtual;
$("b-disconnect").onclick = () => { setConn(null); say("DISCONNECTED: THE SOUNDS STAY HERE, THE SLIDERS PLAY ONLY HERE"); };

// ------------------------------------------------------------------ the tracks: live on the FM-1 and here
function fromDevice(rec, t) {                                // the FM-1's track: its sound, the page's name for it kept
  const s = parseSound(rec, meta), o = tracks[t];
  if (o) s.name = o.name, s.cat = o.cat, s.num = o.num;
  else s.name = "", s.num = 1;
  return s;
}
const homeName = (t) => (label[t] && labelEng[t] === tracks[t].eng ? label[t] : tracks[t].name);   // (what HOME shows)
const trackName = (t) => homeName(t) || `${meta.engines[tracks[t].eng].name} SOUND`;
const engTrack = (t) => engSend([...HEAD, CMD.TRACK, 1, t, ...pack7(buildRecord(tracks[t])), ...Array.from(homeName(t), (c) => c.charCodeAt(0) & 0x7f)]);
const live = new LiveSender(async (t) => { if (conn && conn.live) await conn.client.trackSet(t, buildRecord(tracks[t]), homeName(t)); }, 15);
live.onError = (e) => say(`LIVE: ${errText(e)}`, "err");
function pushTrack(t) {                                      // track t's sound: here, and on the FM-1 (live)
  engTrack(t);
  if (conn && conn.live) live.set(t);
}
function changed() {                                         // the sound in the editor moved
  touchedAt = performance.now();
  pushTrack(selT);
  drawTitle();
}
async function selectTrack(t) {
  selT = t;
  engSend([...HEAD, CMD.TRACK, 2, t]);
  drawTracks(), drawEditor();
  if (conn && conn.live) {
    await live.running;
    try { await conn.client.select(t); } catch (e) { say(`SELECT: ${errText(e)}`, "err"); }
  }
}
let polling = false;
setInterval(async () => {                                    // the FM-1's knobs and track: the page follows (polled)
  if (!conn || !conn.live || conn.kind !== "fm1" || busy || polling || !live.idle || document.hidden) return;
  if (performance.now() - touchedAt < 800 || /SELECT|INPUT/.test(document.activeElement.tagName) && document.activeElement.type !== "range") return;
  polling = true;
  try {
    let { sel, rec, name } = await conn.client.track(selT, true);
    if (sel !== selT) selT = sel, engSend([...HEAD, CMD.TRACK, 2, sel]), { rec, name } = await conn.client.track(sel, true), drawTracks();
    if (!live.idle || performance.now() - touchedAt < 800) return;
    const mine = buildRecord(tracks[selT]);
    if (rec.slice(3, 50).some((x, k) => x !== mine[3 + k]) || name !== homeName(selT)) {
      tracks[selT] = fromDevice(rec, selT), label[selT] = name, labelEng[selT] = tracks[selT].eng;
      engTrack(selT), drawTracks(), drawEditor();
    }
  } catch { /* (the next poll) */ } finally { polling = false; }
}, 400);
function trackTitle(t) {
  const s = tracks[t];
  return !s ? "—" : homeName(t) ? `${homeName(t)} · ${meta.engines[s.eng].name}` : trackName(t);
}
function drawTracks() {
  const b = $("tracks");
  b.textContent = "";
  for (let t = 0; t < 4; t++) {
    const x = el("button", { cls: "trk" + (t === selT ? " on" : "") }, b);
    el("b", {}, x, `T${t + 1}`), x.append(" " + (tracks[t] ? trackTitle(t) : ""));
    x.onclick = () => selectTrack(t);
  }
  drawTitle();
}
function drawTitle() {
  const s = tracks[selT];
  $("ed-title").textContent = `T${selT + 1}: ${s ? trackName(selT) : ""}` + (s && JSON.stringify(s) !== loaded[selT] ? " *" : "");
  $("ed-from").textContent = from[selT] + (conn && conn.live ? ` · LIVE ON THE ${conn.kind === "virtual" ? "VIRTUAL " : ""}FM-1` : conn ? " · HERE ONLY (THE FM-1'S GR!MM HAS NO LIVE EDITING)" : " · HERE ONLY (NOT CONNECTED)");
}

// ------------------------------------------------------------------ sounds
function slotLabel(r, i, fact) {                             // -> [number, the full name, its source ("" empty)]
  const n = String(i + 1).padStart(2, "0");
  if (!fact && isEmpty(r)) return [n, "— EMPTY —", ""];
  const s = parseSound(r, meta), name = fact ? meta.factory[i].name : soundTitle(r, meta);
  return [n, name, meta.engines[s.eng].name];
}
function slotBox(parent, [n, name, src], cls, pickIt, loadIt) {   // a slot: its name whole, its source a tag under it
  const d = el("div", { cls: "slot" + cls, tabindex: 0, role: "button" }, parent);
  el("span", { cls: "n" }, d, n + " "), d.append(name);
  if (src) el("small", { cls: "src" }, d, src);
  d.onclick = pickIt, d.ondblclick = loadIt;
  d.addEventListener("keydown", (e) => { if (e.key === "Enter" || e.key === " ") { e.preventDefault(); pickIt(); } });
  return d;
}
const pickRec = () => (pick.fact ? Uint8Array.from(meta.factory[pick.i].rec) : user[pick.i]);
const pickName = () => (pick.fact ? `FACTORY ${pick.i + 1}: ${meta.factory[pick.i].name}` :
  `SLOT ${String(pick.i + 1).padStart(2, "0")}` + (isEmpty(user[pick.i]) ? " (EMPTY)" : `: ${soundTitle(user[pick.i], meta)}`));
function drawSlots() {
  const u = $("user"), f = $("factory");
  u.textContent = f.textContent = "";
  const kept = document.activeElement && document.activeElement.classList.contains("slot") ?
    [document.activeElement.parentNode.id, [...document.activeElement.parentNode.children].indexOf(document.activeElement)] : null;
  user.forEach((r, i) => {
    const on = !pick.fact && pick.i === i;
    slotBox(u, slotLabel(r, i, false), (isEmpty(r) ? " empty" : "") + (on ? " sel" : "") + (dirty[i] ? " dirty" : ""),
            () => { pick = { fact: false, i }; drawSlots(); }, () => loadTo(selT));
  });
  meta.factory.forEach((x, i) => {
    slotBox(f, slotLabel(Uint8Array.from(x.rec), i, true), pick.fact && pick.i === i ? " sel" : "",
            () => { pick = { fact: true, i }; drawSlots(); }, () => loadTo(selT));
  });
  if (kept) { const k = $(kept[0]).children[kept[1]]; if (k) k.focus(); }   // (a redraw keeps the keyboard where it was)
  $("pickname").textContent = pickName();
  $("readat").textContent = readAt;
  const es = $("ed-slot"), keep = es.value;
  es.textContent = "";
  user.forEach((r, i) => el("option", { value: i }, es, `SLOT ${String(i + 1).padStart(2, "0")}${isEmpty(r) ? " (EMPTY)" : ": " + soundTitle(r, meta)}`));
  es.value = keep || 0;
  refreshButtons();
}
function refreshButtons() {
  const c = !!conn && !busy, u = !pick.fact, empty = u && isEmpty(user[pick.i]);
  ["b-readall", "b-store", "b-backup", "b-restore", "b-sinfo", "b-sfactory", "b-setread", "b-fxread", "sq-import", "sq-now", "sq-send"].forEach((id) => { $(id).disabled = !c; });
  document.querySelectorAll("#fx input, #fx select").forEach((x) => { x.disabled = !c || !fxVals; });
  $("b-setwrite").disabled = !c || !setBytes;
  $("set-mch").disabled = $("set-clk").disabled = $("set-latch").disabled = $("set-vel").disabled = !setBytes;
  if (setBytes && setBytes.length <= SET_V1) $("set-clk").disabled = true;   // (GR!MM 1.0: no CLOCK)
  if (setBytes && setBytes.length <= SET_V2) $("set-latch").disabled = $("set-vel").disabled = true;   // (before V2's
                                                             // SYSTEM 3: no LATCH, no VELOCITY)
  $("b-disconnect").disabled = !conn || busy;
  $("b-connect").disabled = $("b-try").disabled = busy;
  for (let t = 0; t < 4; t++) $("b-load" + t).disabled = empty;
  $("b-export").disabled = empty;
  ["b-rename", "b-clear"].forEach((id) => { $(id).disabled = !u || empty; });
  $("b-import").disabled = !u;
  $("b-send").disabled = !c || !u || !dirty[pick.i];
  document.querySelectorAll(".smp button").forEach((b) => { b.disabled = !c; });
}
async function readSlots() {
  for (let i = 0; i < 64; i++) {
    user[i] = await conn.client.get(OBJ.SND + i, 64);
    dirty[i] = false;
    progress(i + 1, 64);
  }
  readAt = `READ FROM THE ${conn.kind === "virtual" ? "VIRTUAL " : ""}FM-1 AT ${new Date().toLocaleTimeString()}`;
}
$("b-readall").onclick = () => job("READ", async () => {
  await readSlots();
  drawSlots();
  return `READ: ${user.filter((r) => !isEmpty(r)).length} OF 64 USER SLOTS HOLD A SOUND`;
});
async function writeSlot(i, rec) {                           // flash: a user slot, read back
  await conn.client.put(OBJ.SND + i, rec, conn.info.fmt, progress);
  const back = await conn.client.get(OBJ.SND + i, 64);
  if (back.some((x, k) => x !== rec[k])) throw new EditorError("THE SLOT DID NOT READ BACK AS SENT", 0);
  user[i] = rec, dirty[i] = false;
  drawSlots();
}
$("b-send").onclick = () => job(`SEND SLOT ${pick.i + 1}`, async () => {
  const i = pick.i;
  await writeSlot(i, user[i]);
  return `SLOT ${i + 1} SENT AND READ BACK: ${isEmpty(user[i]) ? "EMPTY" : soundTitle(user[i], meta)}`;
});
function loadTo(t) {                                         // the chosen sound onto track t, here and on the FM-1
  const r = pickRec();
  if (isEmpty(r)) return;
  const s = parseSound(r, meta);
  if (pick.fact) s.name = meta.factory[pick.i].name.slice(0, 10);
  tracks[t] = s, loaded[t] = JSON.stringify(s), from[t] = pickName();
  label[t] = pick.fact ? meta.factory[pick.i].name : soundTitle(r, meta), labelEng[t] = s.eng;   // (as PRESETS names it)
  pushTrack(t);
  selectTrack(t);
  show("editor");
  history.replaceState(null, "", "#editor");
  say(`${pickName()} ON TRACK ${t + 1}` + (conn && conn.live ? " (ON THE FM-1 TOO, NOT SAVED)" : " (HERE)"), "ok");
}
for (let t = 0; t < 4; t++) $("b-load" + t).onclick = () => loadTo(t);
$("b-rename").onclick = () => {
  const s = parseSound(user[pick.i], meta), n = prompt("A NAME (10 CHARACTERS: A-Z 0-9 SPACE); THE FM-1 ITSELF SHOWS \"" +
    `${meta.cats[s.cat]} ${String(s.num).padStart(2, "0")}\"`, s.name || "");
  if (n === null) return;
  s.name = n.toUpperCase().replace(/[^A-Z0-9 \-!#+.]/g, "").slice(0, 10);
  user[pick.i] = buildRecord(s), dirty[pick.i] = true;
  drawSlots();
  say(`SLOT ${pick.i + 1} RENAMED HERE (SEND TO FM-1 WRITES IT)`, "ok");
};
$("b-clear").onclick = () => {
  if (!confirm(`CLEAR SLOT ${pick.i + 1} HERE? (THE FM-1 KEEPS IT UNTIL YOU SEND)`)) return;
  user[pick.i] = emptyRecord(), dirty[pick.i] = true;
  drawSlots();
  say(`SLOT ${pick.i + 1} CLEARED HERE (SEND TO FM-1 WRITES IT)`, "ok");
};
$("b-export").onclick = () => exportSound(pickRec(), pick.fact ? meta.factory[pick.i].name : null);
$("b-import").onclick = () => $("f-import").click();
async function readSoundFile(f) {
  const j = JSON.parse(await f.text()), r = Uint8Array.from(j.rec || []);
  if (j.format !== "grimm-sound" || r.length !== 64 || !recordOk(r)) throw new Error("NOT A GR!MM SOUND FILE");
  return r;
}
$("f-import").onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = "";
  if (!f) return;
  try {
    const r = await readSoundFile(f);
    user[pick.i] = r, dirty[pick.i] = true;
    drawSlots();
    say(`IMPORTED INTO SLOT ${pick.i + 1}: ${soundTitle(r, meta)} (SEND TO FM-1 WRITES IT)`, "ok");
  } catch (err) { say(`IMPORT: ${errText(err)}`, "err"); }
};
function exportSound(r, name) {
  const t = name || soundTitle(r, meta) || "SOUND";
  download(`${t.replace(/[^A-Z0-9]+/g, "_")}.grimmsound`, JSON.stringify({ format: "grimm-sound", version: 1, name: t, rec: [...r] }));
  say(`EXPORTED: ${t}`, "ok");
}

// ------------------------------------------------------------------ the sound editor
const knobText = (s, k) => vf.text("web_knob_text", s.eng, s.k[0], s.k[1], s.k[2], s.k[3], k);
function control(parent, label, min, max, get, set, text, quiet = false) {   // (quiet: not the sound's: FX)
  const c = el("div", { cls: "ctl" }, parent), lab = el("div", { cls: "lab" }, c), v = el("span");
  el("label", {}, lab, label), lab.appendChild(v);
  const r = el("input", { type: "range", min, max, step: 1 }, c);
  r.value = get();
  const upd = () => { v.textContent = text ? text(+r.value) : String(r.value); };
  r.addEventListener("input", () => { set(+r.value); upd(); if (!quiet) changed(); });
  upd();
  return r;
}
function choice(parent, label, names, get, set, quiet = false) {
  const c = el("div", { cls: "ctl" }, parent), lab = el("div", { cls: "lab" }, c);
  el("label", {}, lab, label);
  const s = el("select", {}, c);
  s.style.width = "100%";
  names.forEach((n, i) => el("option", { value: i }, s, n));
  s.value = get();
  s.addEventListener("change", () => { set(+s.value); if (!quiet) changed(); });
  return s;
}
function toggle(parent, names, get, set) {
  const t = el("button", { cls: "toggle" }, parent);
  const upd = () => { t.textContent = names[get() ? 1 : 0]; t.classList.toggle("on", !!get()); };
  t.onclick = () => { set(get() ? 0 : 1); upd(); changed(); };
  upd();
}
function drawEditor() {
  const ed = $("ed"), s = tracks[selT];
  ed.textContent = "";
  if (!s) return;
  const eng = meta.engines[s.eng];
  let g = el("div", { cls: "grp" }, ed);
  el("h3", {}, g, "SOUND");
  const c = el("div", { cls: "ctl" }, g), lab = el("div", { cls: "lab" }, c);
  el("label", {}, lab, "NAME (THIS EDITOR'S)");
  const nm = el("input", { type: "text", maxlength: 10 }, c);
  nm.style.width = "100%", nm.value = s.name || "";
  nm.addEventListener("input", () => {                     // (10 characters the FM-1 shows: A-Z 0-9 space - ! # + .)
    const at = nm.selectionStart;
    nm.value = s.name = nm.value.toUpperCase().replace(/[^A-Z0-9 \-!#+.]/g, "").slice(0, 10);
    nm.setSelectionRange(at, at);
    label[selT] = s.name.trim(), labelEng[selT] = s.eng;     // (the FM-1's HOME shows it too)
    changed(), drawTracks();
  });
  const cat = el("div", { cls: "ctl" }, g), cl = el("div", { cls: "lab" }, cat);
  el("label", {}, cl, "CATEGORY (FOR STORE)");
  const cs = el("select", {}, cat);
  cs.style.width = "100%";
  meta.cats.forEach((n, i) => el("option", { value: i }, cs, n));
  cs.value = s.cat;
  cs.addEventListener("change", () => { s.cat = +cs.value; drawTitle(); drawTracks(); });
  choice(g, "SOURCE", meta.engines.map((e) => e.name), () => s.eng, (v) => {
    s.eng = v;
    for (let k = 0; k < 4; k++) s.k[k] = Math.min(s.k[k], meta.engines[v].max[k]);
    if (v === 1) setPatch(s, s.k[0]);
    drawEditor(), drawTracks();
  });
  toggle(g, ["LATCH OFF", "LATCH ON"], () => s.latch, (v) => { s.latch = v; });
  const V2 = V2_SOUND_CONTROLS;                               // (GR!MM 2.0's settings: names, ranges as on the FM-1)
  control(g, V2.glide[0], V2.glide[1], V2.glide[2], () => s.glide || 0, (v) => { s.glide = v; }, glideText);   // (SOURCE's KNOB 4: every source)
  if (s.eng === 3)                                           // (VOICE's TYPE, as SOURCE's KNOB 3)
    choice(g, V2.vtype[0], VOICE_TYPES, () => s.vtype || 0, (v) => { s.vtype = v; });
  if (s.eng === 6) {                                         // (PHASE's LINE 2 page, then PHASE ENV)
    s.pd = s.pd || { ...PHASE_PD0 };
    const p2 = el("div", { cls: "grp" }, ed), [lw, ld, lr, ln] = V2.line2;
    el("h3", {}, p2, "PHASE LINE 2");
    choice(p2, lw[0], LINE2_WAVES, () => s.pd.wave || 0, (v) => { s.pd.wave = v; });
    control(p2, ld[0], ld[1], ld[2], () => s.pd.dcw || 0, (v) => { s.pd.dcw = v; }, (v) => pctText(v, "SAME"));
    toggle(p2, [`${lr[0]} OFF`, `${lr[0]} ON`], () => s.pd.ring, (v) => { s.pd.ring = v; });
    toggle(p2, [`${ln[0]} OFF`, `${ln[0]} ON`], () => s.pd.noise, (v) => { s.pd.noise = v; });
    const pe = el("div", { cls: "grp" }, ed), [de, dd, pi, pp] = V2.env;
    el("h3", {}, pe, "PHASE ENV");
    control(pe, de[0], de[1], de[2], () => s.pd.denv || 0, (v) => { s.pd.denv = v; }, (v) => pctText(v, "OFF"));
    control(pe, dd[0], dd[1], dd[2], () => s.pd.ddec || 0, (v) => { s.pd.ddec = v; }, decayText);
    control(pe, pi[0], pi[1], pi[2], () => s.pd.penv || 0, (v) => { s.pd.penv = v; }, PITCH_TEXT);
    control(pe, pp[0], pp[1], pp[2], () => s.pd.pdec || 0, (v) => { s.pd.pdec = v; }, decayText);
  }

  g = el("div", { cls: "grp" }, ed);
  el("h3", {}, g, `${eng.name}: ITS FOUR KNOBS`);
  for (let k = 0; k < 4; k++)
    control(g, eng.knobs[k], 0, eng.max[k], () => s.k[k], (v) => {
      s.k[k] = v;
      if (s.eng === 1 && k === 0) { setPatch(s, v); drawFM(); }   // (FM's PATCH: its operators as built, as on the FM-1)
    }, () => knobText(s, k));

  g = el("div", { cls: "grp" }, ed);
  el("h3", {}, g, "ENV");
  ["ATTACK", "DECAY", "SUSTAIN", "RELEASE"].forEach((n, k) =>
    control(g, n, 0, 127, () => s.env[k], (v) => { s.env[k] = v; }, (v) => vf.text("web_env_text", k, v).toUpperCase()));

  g = el("div", { cls: "grp" }, ed);
  el("h3", {}, g, "LFO");
  control(g, "RATE", 0, 127, () => s.lfo[0], (v) => { s.lfo[0] = v; }, (v) => vf.text("web_lfo_text", v).toUpperCase());
  control(g, "DEPTH", 0, 100, () => s.lfo[1], (v) => { s.lfo[1] = v; }, (v) => `${v} %`);
  choice(g, "DESTINATION", meta.lfoDest.map((d, i) => (i >= 3 && i <= 6 ? eng.knobs[i - 3] : d)), () => s.lfo[2], (v) => { s.lfo[2] = v; });
  choice(g, "SHAPE", meta.lfoShape, () => s.lfo[3], (v) => { s.lfo[3] = v; });

  g = el("div", { cls: "grp" }, ed);
  el("h3", {}, g, "CHORD");
  toggle(g, ["CHORD MODE OFF", "CHORD MODE ON"], () => s.chordOn, (v) => { s.chordOn = v; });
  el("div", { cls: "ctl" }, g);
  choice(g, "CHORD TYPE", meta.chords, () => s.chord, (v) => { s.chord = v; });
  choice(g, "VOICING", meta.voicings, () => s.voicing, (v) => { s.voicing = v; });
  drawFM();
  drawTitle();
}
function setPatch(s, v) {
  const f = meta.fmPatches[Math.min(v, meta.fmPatches.length - 1)].fm;
  s.fm = { alg: f[0] & 31, fb: f[1] & 7, ops: [0, 1, 2, 3, 4, 5].map((i) => f.slice(2 + i * 5, 7 + i * 5)) };
}
const ratioText = (o) => { const r = (o[0] ? o[0] * 100 : 50) * (100 + o[1]) / 100; return (r >= 1000 ? (r / 100).toFixed(1) : (r / 100).toFixed(2)); };
function drawFM() {
  const f = $("fm"), s = tracks[selT];
  f.textContent = "";
  f.style.display = s && s.eng === 1 ? "" : "none";
  if (!s || s.eng !== 1) return;
  el("h3", {}, f, "FM: THE TRACK'S OWN PATCH (FM ALGO, FM OP)");
  const top = el("div", { cls: "grid" }, f), a = el("div", {}, top), b = el("div", {}, top);
  const cv = el("canvas", { id: "alg", width: 216, height: 104 }, b);
  control(a, "ALGO", 0, 31, () => s.fm.alg, (v) => { s.fm.alg = v; drawAlg(cv); }, (v) => String(v + 1).padStart(2, "0"));
  control(a, "FEEDBACK", 0, 7, () => s.fm.fb, (v) => { s.fm.fb = v; drawAlg(cv); });
  const t = el("table", { cls: "ops" }, el("div", { cls: "ops-box" }, f)), h = el("tr", {}, t);
  ["", "RATIO", "", "FINE", "", "LEVEL", "", "ATTACK", "", "DECAY", ""].forEach((x) => el("td", {}, h, x));
  s.fm.ops.forEach((o, i) => {
    const r = el("tr", {}, t);
    el("td", { cls: "o" }, r, `OP${i + 1}`);
    const vr = [];
    [[31, 0], [99, 1], [99, 2], [99, 3], [99, 4]].forEach(([mx, k]) => {
      const c = el("td", {}, r), v = el("td", { cls: "v" }, r), in_ = el("input", { type: "range", min: 0, max: mx, step: 1 }, c);
      in_.value = o[k];
      vr.push(v);
      const upd = () => { v.textContent = k === 0 ? ratioText(o) : String(o[k]); if (k === 1) vr[0].textContent = ratioText(o); };
      in_.addEventListener("input", () => { o[k] = +in_.value; upd(); changed(); });
      upd();
    });
  });
  drawAlg(cv);
}
function drawAlg(cv) {                                       // ui.c's fm_algo_draw, here: the operators as wired
  const s = tracks[selT], g = cv.getContext("2d"), alg = meta.fmAlg[s.fm.alg], car = [], tg = [0, 0, 0, 0, 0, 0], bus = [0, 0, 0];
  let fbin = -1;
  for (let k = 0; k < 6; k++) {
    const f = alg[k], o = 5 - k, ib = (f >> 4) & 3, ob = f & 3;
    if (ib) for (let j = 0; j < 6; j++) if (bus[ib] >> j & 1) tg[j] |= 1 << o;
    if (f & 0x40) fbin = o;
    if (ob) bus[ob] = (f & 4 ? bus[ob] : 0) | 1 << o; else car.push(o);
  }
  const dep = [0, 0, 0, 0, 0, 0], x2 = [-1, -1, -1, -1, -1, -1];
  for (let r = 0; r < 6; r++) for (let o = 0; o < 6; o++) for (let j = 0; j < 6; j++) if (tg[o] >> j & 1 && dep[j] + 1 > dep[o]) dep[o] = dep[j] + 1;
  let slot = 0;
  const place = (o) => {
    let lo = 999, hi = -1;
    for (let j = 0; j < 6; j++) {
      let first = 0;
      while (first < 6 && !(tg[j] >> first & 1)) first++;
      if (first !== o || x2[j] >= 0) continue;
      place(j);
      lo = Math.min(lo, x2[j]), hi = Math.max(hi, x2[j]);
    }
    x2[o] = hi < 0 ? 2 * slot++ : (lo + hi) >> 1;
  };
  car.slice().sort((a, b) => a - b).forEach(place);
  for (let o = 0; o < 6; o++) {
    let a = 999, b = -1, n = 0;
    for (let j = 0; j < 6; j++) if (tg[o] >> j & 1) a = Math.min(a, x2[j]), b = Math.max(b, x2[j]), n++;
    if (n > 1) x2[o] = (a + b) >> 1;
    if (x2[o] < 0) x2[o] = 2 * slot++;
  }
  const C = getComputedStyle(document.documentElement), col = (v) => C.getPropertyValue(v).trim();
  const w = Math.floor(216 / Math.max(1, slot)), bw = Math.min(w - 6, 28), X = (o) => Math.floor(w / 2 + x2[o] * w / 2), Y = (o) => 70 - dep[o] * 22;
  g.fillStyle = col("--k"), g.fillRect(0, 0, 216, 104);
  g.fillStyle = col("--m");
  for (let o = 0; o < 6; o++) for (let j = 0; j < 6; j++) if (tg[o] >> j & 1) {
    const xa = X(o), xb = X(j), ym = Y(j) - 4;
    g.fillRect(xa - 1, Y(o) + 14, 2, ym - Y(o) - 14), g.fillRect(Math.min(xa, xb) - 1, ym, Math.abs(xa - xb) + 2, 2), g.fillRect(xb - 1, ym, 2, 4);
  }
  g.font = '8px "Press Start 2P", monospace';
  let lo = 999, hi = -1;
  for (let o = 0; o < 6; o++) {
    const x = X(o) - (bw >> 1), y = Y(o), c = car.includes(o);
    if (c) {
      lo = Math.min(lo, X(o)), hi = Math.max(hi, X(o));
      g.fillStyle = col("--l"), g.fillRect(X(o) - 1, y + 14, 2, 8);
      g.fillStyle = col("--a"), g.fillRect(x, y, bw, 14), g.fillStyle = col("--k"), g.fillRect(x + 2, y + 2, bw - 4, 10);
      g.fillStyle = col("--a");
    } else { g.fillStyle = col("--d"), g.fillRect(x, y, bw, 14); g.fillStyle = col("--l"); }
    g.fillText(String(o + 1), X(o) - 4, y + 11);
    if (o === fbin && s.fm.fb) { g.fillStyle = col("--m"); g.fillRect(x + bw, y + 6, 4, 2), g.fillRect(x + bw + 2, y - 4, 2, 10), g.fillRect(X(o), y - 4, x + bw + 4 - X(o), 2); }
  }
  g.fillStyle = col("--l"), g.fillRect(lo - 1, 92, hi - lo + 2, 2);
  g.fillStyle = col("--m"), g.fillText("OUT", 96, 103);
}
$("b-store").onclick = () => {                               // flash: the track's sound into a user slot
  const s = tracks[selT], i = +$("ed-slot").value;
  if (!s || !conn) return;
  if (!isEmpty(user[i]) && !confirm(`SLOT ${i + 1} HOLDS ${soundTitle(user[i], meta)}: REPLACE IT ON THE FM-1?`)) return;
  job(`STORE IN SLOT ${i + 1}`, async () => {
    let num = 0;                                             // (its number: one past the category's highest, as the FM-1)
    user.forEach((r, k) => { if (k !== i && !isEmpty(r) && r[1] === s.cat && r[2] > num) num = r[2]; });
    s.num = Math.min(99, num + 1);
    const rec = buildRecord(s);
    await writeSlot(i, rec);
    loaded[selT] = JSON.stringify(s), from[selT] = `SLOT ${String(i + 1).padStart(2, "0")}`;
    drawTitle();
    return `T${selT + 1} STORED IN SLOT ${i + 1} ON THE FM-1 AND READ BACK: ${soundTitle(rec, meta)}`;
  });
};
$("b-eexport").onclick = () => { if (tracks[selT]) exportSound(buildRecord(tracks[selT])); };
$("b-eimport").onclick = () => $("f-eimport").click();
$("f-eimport").onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = "";
  if (!f) return;
  try {
    const s = parseSound(await readSoundFile(f), meta);
    tracks[selT] = s, loaded[selT] = JSON.stringify(s), from[selT] = f.name.toUpperCase();
    label[selT] = s.name, labelEng[selT] = s.eng;
    pushTrack(selT), drawTracks(), drawEditor();
    say(`${f.name.toUpperCase()} ON TRACK ${selT + 1}`, "ok");
  } catch (err) { say(`IMPORT: ${errText(err)}`, "err"); }
};
$("b-revert").onclick = () => {
  if (!loaded[selT]) return;
  tracks[selT] = JSON.parse(loaded[selT]);
  pushTrack(selT), drawTracks(), drawEditor();
  say(`T${selT + 1}: AS LOADED`, "ok");
};

// ------------------------------------------------------------------ play: the engine's keys (the selected track)
let keyMask = 0;
const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26], BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
const BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];
const NOTE = ["F", "F#", "G", "G#", "A", "A#", "B", "C", "C#", "D", "D#", "E"];
const KEYMAP = {};
"zxcvbnm,./".split("").forEach((c, i) => { KEYMAP[c] = WHITE[i]; });
"sdfhjl;'".split("").forEach((c, i) => { KEYMAP[c] = [1, 3, 5, 8, 10, 13, 15, 17][i]; });
"qwertyuio".split("").forEach((c, i) => { KEYMAP[c] = 18 + i; });
const keyEls = [];
function drawKeys() {
  const k = $("kbd"), ww = 100 / WHITE.length;
  WHITE.forEach((key, i) => {
    const e = el("div", { cls: "w" }, k);
    Object.assign(e.style, { left: `${i * ww}%`, width: `${ww}%` });
    if (NOTE[key % 12] === "C") el("span", {}, e, "C" + (3 + Math.floor((key + 5) / 12)));
    keyEls[key] = e;
  });
  BLACK.forEach((key, i) => {
    const e = el("div", { cls: "b" }, k);
    Object.assign(e.style, { left: `${(BLACK_AFTER[i] + 0.68) * ww}%`, width: `${ww * 0.64}%` });
    keyEls[key] = e;
  });
  keyEls.forEach((e, key) => {
    e.addEventListener("pointerdown", (ev) => { e.setPointerCapture(ev.pointerId); press(key, true); });
    e.addEventListener("pointerup", () => press(key, false));
    e.addEventListener("pointercancel", () => press(key, false));
  });
}
function press(key, down) {
  keyMask = down ? keyMask | 1 << key : keyMask & ~(1 << key);
  keyEls[key].classList.toggle("down", down);
  if (node) node.port.postMessage({ type: "keys", mask: keyMask >>> 0 });
}
addEventListener("keydown", (e) => {
  const k = KEYMAP[e.key.toLowerCase()];
  if (k === undefined || e.repeat || (sq.rec && sqOn()) || e.ctrlKey || e.metaKey || e.altKey || /INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName) &&
      document.activeElement.type !== "range") return;
  e.preventDefault();
  press(k, true);
});
addEventListener("keyup", (e) => { const k = KEYMAP[e.key.toLowerCase()]; if (k !== undefined && keyMask >> k & 1) press(k, false); });
$("b-panic").onclick = () => { if (node) node.port.postMessage({ type: "panic" }); say("ALL NOTES OFF (HERE)", "ok"); };

// ------------------------------------------------------------------ samples
let smpInfo = [];
async function sampleInfo() {
  if (!conn || busy) return;
  try { smpInfo = await conn.client.smpInfo(); } catch (e) { say(`SAMPLES: ${errText(e)}`, "err"); return; }
  drawSamples();
}
function drawSamples() {
  const box = $("smp");
  box.textContent = "";
  if (!conn) { el("p", { cls: "note" }, box, "Connect first (CONNECT)."); return; }
  smpInfo.forEach((s, k) => {
    const d = el("div", { cls: "smp" }, box);
    el("div", { cls: "big px" }, d, String(k + 1));
    const r = el("div", {}, d);
    el("div", { cls: "px" }, r, s.state === 1 ? `${s.name || "(NO NAME)"} · ${(s.n / 22050).toFixed(2)} S` +
      (s.root !== null ? ` · ROOT ${noteName(s.root)}` : "") : s.state === 2 ? "BEING CHECKED…" :
      s.state < 0 ? "IN USE (ITS NAME DID NOT FIT IN THE FM-1'S ANSWER: GR!MM BEFORE 26B2)" : "EMPTY");
    const drop = el("div", { cls: "drop px" }, r, "DROP A WAV HERE, OR CLICK");
    const file = el("input", { type: "file", accept: ".wav,audio/wav" }, r);
    file.hidden = true;
    const row = el("div", { cls: "row" }, r);
    el("label", {}, row, "NAME");
    const nm = el("input", { type: "text", maxlength: 8, size: 9 }, row);   // (8: the slot's, what the FM-1 shows)
    nm.value = s.state === 1 ? s.name : "";
    nm.addEventListener("input", () => { nm.value = nm.value.toUpperCase().replace(/[^A-Z0-9 _\-.#!+]/g, "").slice(0, 8); });
    el("label", {}, row, "ROOT");
    const root = el("select", {}, row);                      // (the note the sample plays at its own pitch)
    for (let n = 0; n < 128; n++) el("option", { value: n }, root, `${noteName(n)} (${n})`);
    root.value = s.state === 1 && s.root !== null ? s.root : 60;
    const up = el("button", { cls: "go" }, row, "UPLOAD"), er = el("button", {}, row, "ERASE");
    let buf = null;
    const take = async (f) => {
      if (!f) return;
      buf = await f.arrayBuffer();
      const stem = f.name.replace(/\.[^.]*$/, "");
      nm.value = sampleName(stem), root.value = noteFromName(stem, +root.value);
      drop.textContent = f.name.toUpperCase();
    };
    drop.onclick = () => file.click();
    file.onchange = () => take(file.files[0]);
    drop.ondragover = (e) => { e.preventDefault(); drop.classList.add("hot"); };
    drop.ondragleave = () => drop.classList.remove("hot");
    drop.ondrop = (e) => { e.preventDefault(); drop.classList.remove("hot"); take(e.dataTransfer.files[0]); };
    up.onclick = () => job(`UPLOAD SLOT ${k + 1}`, async () => {
      if (!buf) throw new EditorError("CHOOSE A WAV FIRST", 0);
      const sl = makeSlot(buf, sampleName(nm.value), Math.max(0, Math.min(127, +root.value | 0)), $("smp-norm").checked);
      say(`UPLOADING ${(sl.data.length / 1024).toFixed(0)} KB…`);
      smpInfo = await conn.client.smpUpload(k, sl.hdr, sl.data, progress);
      drawSamples();
      if (smpInfo[k].state === 0 || smpInfo[k].state === 2) throw new EditorError("THE FM-1 DID NOT ACCEPT IT: UPLOAD AGAIN", 0);
      return `SLOT ${k + 1}: ${smpInfo[k].name || sampleName(nm.value)}, ${sl.secs.toFixed(2)} S${sl.cut ? " (CUT TO 6.64 S)" : ""}, CHECKED BY THE FM-1`;
    });
    er.onclick = () => {
      if (!confirm(`ERASE SAMPLE SLOT ${k + 1} ON THE FM-1?`)) return;
      job(`ERASE SLOT ${k + 1}`, async () => { await conn.client.smpErase(k); smpInfo = await conn.client.smpInfo(); drawSamples(); return `SLOT ${k + 1} ERASED`; });
    };
  });
  refreshButtons();
}
$("b-sinfo").onclick = sampleInfo;
$("b-sfactory").onclick = () => {                            // RESTORE: the four factory samples, over whatever is there
  if (!confirm("RESTORE THE FACTORY SAMPLES?\n\nThis REPLACES all four sample slots on the FM-1 with GR!MM's factory samples " +
               "(URCHIN, LEAVE, NIKOLAI, M-SOFT). Your own samples in them are gone: keep their WAV files.")) return;
  job("RESTORE FACTORY SAMPLES", async () => {
    say("WRITING THE FOUR FACTORY SAMPLES (ABOUT 5 S)…");
    try { smpInfo = await conn.client.smpFactory(); } catch (e) {
      if (e.rc === 97) throw new EditorError("NO ANSWER: THE FM-1'S GR!MM IS OLDER THAN 26C (NO FACTORY SAMPLES)", 97);
      throw e;
    }
    drawSamples();
    return `FACTORY SAMPLES RESTORED: ${smpInfo.map((x) => x.name || "-").join(", ")}`;
  });
};

// ------------------------------------------------------------------ backup
$("b-backup").onclick = () => job("BACKUP", async () => {
  const b = await makeBackup(conn.client, conn.info, (i, n, what) => { progress(i, n); say(`READING ${what.toUpperCase()}…`); });
  download(`grimm-backup-${stamp()}.json`, JSON.stringify(b, null, 1));
  return `BACKUP MADE: ALL 64 SOUNDS, SLOTS A–D, THE SETTINGS${b.samples ? ", THE SAMPLES" : " (NOT THE SAMPLES: THIS GR!MM CANNOT SEND THEM)"} (DOWNLOADED)` +
         (conn.client.missed ? `; ${conn.client.missed} REPLIES MISSED, ASKED AGAIN` : "");
});
$("b-restore").onclick = () => $("f-restore").click();
$("f-restore").onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = "";
  if (!f) return;
  let objs, smp;
  try {
    const b = JSON.parse(await f.text());
    objs = checkBackup(b, conn.info), smp = checkSamples(b, conn.info);
  } catch (err) { say(`RESTORE: ${errText(err)} (NOTHING WRITTEN)`, "err"); return; }
  if (!confirm(`RESTORE ${f.name}?\n\nThis REPLACES all 64 user sounds, slots A–D and the settings on the FM-1` +
               (smp ? ", and the four sample slots (as in the file; one already the same is not written again)" :
                 " (the file has no samples: the sample slots stay as they are)") +
               ". A backup of what it holds now is made (downloaded) first.")) return;
  await job("RESTORE", async () => {
    const before = await makeBackup(conn.client, conn.info, (i, n) => { progress(i, n); say("FIRST, A BACKUP OF WHAT THE FM-1 HOLDS NOW…"); });
    download(`grimm-backup-before-restore-${stamp()}.json`, JSON.stringify(before, null, 1));
    await restoreBackup(conn.client, objs, conn.info, (i, n, what) => { progress(i, n); say(`WRITING ${what.toUpperCase()}…`); });
    for (let i = 0; i < objs.length; i++) {                  // read back: as written
      const set = objs[i].id === OBJ.SET, d = await conn.client.get(objs[i].id, set ? conn.info.setSize : objs[i].data.length);
      if (set ? !sameSettings(objs[i].data, d) : d.some((x, k) => x !== objs[i].data[k]))
        throw new EditorError(`${objs[i].what.toUpperCase()} DID NOT READ BACK AS WRITTEN`, 0);
      progress(i + 1, objs.length);
    }
    for (const o of objs) if (o.id >= OBJ.SND && o.id < OBJ.SND + 64) { user[o.id - OBJ.SND] = o.data; dirty[o.id - OBJ.SND] = false; }
    drawSlots();
    let sd = "";
    if (smp) {                                               // GR!MM V2: the sample slots as in the file, each checked
      const done = await restoreSamples(conn.client, smp, (k, i, n) => { progress(i, n); say(`WRITING SAMPLE ${k + 1}…`); });
      smpInfo = await conn.client.smpInfo();
      drawSamples();
      sd = `; SAMPLES: ${done.map((w, k) => `${k + 1} ${w}`).join(", ")}`;
    }
    return `RESTORED AND READ BACK: EVERYTHING AS IN THE FILE${sd} (THE BACKUP FROM BEFORE IS DOWNLOADED)`;
  });
};

// ------------------------------------------------------------------ settings (GR!MM V2): MIDI CHANNEL, CLOCK, LATCH
function drawSettings() {
  const m = $("set-mch"), c = $("set-clk"), l = $("set-latch"), ve = $("set-vel"), v2 = !setBytes || setBytes.length > SET_V1;
  m.textContent = c.textContent = l.textContent = ve.textContent = "";
  MCH_NAMES.forEach((n, i) => { if (v2 || i <= 13) el("option", { value: i }, m, n); });
  CLOCK_NAMES.forEach((n, i) => el("option", { value: i }, c, n));
  ["FROM SOUND", "ALWAYS OFF"].forEach((n, i) => el("option", { value: i }, l, n));
  ["OFF", "ON"].forEach((n, i) => el("option", { value: i }, ve, n));
  if (setBytes) { const s = settingsOf(setBytes); m.value = s.mch; c.value = s.clk; l.value = s.latch; ve.value = s.velocity; }
  refreshButtons();
}
$("b-setread").onclick = () => job("SETTINGS", async () => {
  setBytes = await conn.client.get(OBJ.SET, conn.info.setSize);
  drawSettings();
  return "SETTINGS READ FROM THE FM-1";
});
$("b-setwrite").onclick = () => job("SETTINGS", async () => {
  const b = withSettings(setBytes, { mch: +$("set-mch").value, clk: +$("set-clk").value, latch: +$("set-latch").value,
                                     velocity: +$("set-vel").value });
  await conn.client.put(OBJ.SET, b, conn.info.fmt, progress);
  const back = await conn.client.get(OBJ.SET, conn.info.setSize);
  if (!sameSettings(b, back)) throw new EditorError("THE SETTINGS DID NOT READ BACK AS SENT", 0);
  setBytes = back;
  drawSettings();
  return `SETTINGS WRITTEN: MIDI CHANNEL ${MCH_NAMES[b[3]]}${b.length > SET_V1 ? ", CLOCK " + CLOCK_NAMES[b[SET_V1]] : ""}` +
         (b.length > SET_V2 ? `, LATCH ${b[SET_V2] & 1 ? "ALWAYS OFF" : "FROM SOUND"}, VELOCITY ${b[SET_V2] & 2 ? "ON" : "OFF"}` : "");
});
drawSettings();

// ------------------------------------------------------------------ FX (GR!MM V2): the six pages and the tracks' filters,
// live: a move sends its value (the newest kept while one is on its way)
const fxPend = new Map();
let fxBusy = false;
async function fxSend(i, v) {
  fxPend.set(i, v);
  if (fxBusy || !conn) return;
  fxBusy = true;
  try {
    while (fxPend.size) {
      const [k, x] = fxPend.entries().next().value;
      fxPend.delete(k);
      await conn.client.fxSet(k, x);
    }
  } catch (e) { say(`FX: ${errText(e)}`, "err"); } finally { fxBusy = false; }
}
function drawFx() {
  const box = $("fx"), v = fxVals || new Array(48).fill(0);
  box.textContent = "";
  FX_PAGES.forEach(([page, knobs], p) => {
    const g = el("div", { cls: "grp" }, box);
    el("h3", {}, g, page);
    knobs.forEach((kn, k) => {
      const i = p * 4 + k, set = (x) => { v[i] = x; fxSend(i, x); };
      if (Array.isArray(kn)) choice(g, kn[0], kn[1], () => v[i], set, true);
      else control(g, kn, 0, 127, () => v[i], set, (x) => `${Math.round(x * 100 / 127)}%`, true);
    });
  });
  const g = el("div", { cls: "grp" }, box);
  el("h3", {}, g, "TRACK FILTER (MIXER 2)");
  for (let t = 0; t < 4; t++) {
    const i = 24 + t;
    choice(g, `T${t + 1} TYPE`, FT_NAMES, () => v[i] & 3, (x) => { v[i] = (v[i] & 4) | x; fxSend(i, v[i]); }, true);
    choice(g, `T${t + 1} POS`, ["PRE", "POST"], () => v[i] >> 2 & 1, (x) => { v[i] = (v[i] & 3) | x << 2; fxSend(i, v[i]); }, true);
  }
  if (v[28] !== null) {                                      // (GR!MM 2.0: the tracks' sends, MIXER 1's squares)
    const s = el("div", { cls: "grp" }, box);
    el("h3", {}, s, "TRACK SENDS (MIXER 1)");
    for (let t = 0; t < 4; t++)                              // (as MIXER 1's tiles: DELAY, REVERB, TAPE, GRAIN, DRIVE)
      [["DLY", 28 + t * 4], ["REV", 29 + t * 4], ["TAPE", 44 + t], ["GRAIN", 30 + t * 4], ["DRIVE", 31 + t * 4]].forEach(([nm, i]) => {
        if (v[i] !== null)
          control(s, `T${t + 1} ${nm}`, 0, 127, () => v[i], (x) => { v[i] = x; fxSend(i, x); }, (x) => `${Math.round(x * 100 / 127)}%`, true);
      });
  }
  refreshButtons();
}
$("b-fxread").onclick = () => job("FX", async () => {
  try { fxVals = await conn.client.fx(); } catch (e) {
    if (e instanceof EditorError) throw e;
    throw new EditorError("NO ANSWER: FX NEEDS GR!MM V2 ON THE FM-1", 0);
  }
  drawFx();
  return "FX READ FROM THE FM-1: EVERY MOVE NOW PLAYS ON IT AT ONCE";
});
drawFx();

// ------------------------------------------------------------------ the SEQUENCER (GR!MM 2.0, patterns only: no firmware change)
// The four tracks' patterns: a 64-step grid a track, a step's NOTE, CHANCE, RATCHET, LENGTH, chord and locks, a track's STEPS,
// DIVISION, SWING and PLAY MODE. They are read from a slot (or from the working project, what the FM-1 plays now), written back
// into a slot (only its sequence and lock bytes 212-2403: the sounds and mix stay), kept in a file, or tried in this page's GR!MM.
const sq = { pats: [0, 1, 2, 3].map(() => emptyPattern(16)), t: 0, s: 0, trk: null, from: "EMPTY", dirty: false, clip: null,
             rec: false, oct: 4, mon: true, pedal: false };   // (REC armed, the computer keys' octave, MONITOR, the sustain pedal)
const SQ_SLOT_KEY = ["F3", "G3", "A3", "B3"];
const sqPat = () => sq.pats[sq.t];
const sqTrk = (t) => (sq.trk ? sq.trk[t] : tracks[t] ? { eng: tracks[t].eng, k: tracks[t].k } : { eng: 0, k: [0, 0, 0, 0] });   // (for the locks' names)
function sqStatus() {
  $("sq-from").textContent = `${sq.from}${sq.dirty ? " * (NOT SENT)" : ""}`;
}
function setSq(pats, trk, from, dirty) {
  sq.pats = pats, sq.trk = trk, sq.from = from, sq.dirty = dirty, sq.s = 0;
  drawSeq();
}
function sqCell(i) {                                         // one step's button: its number, its note; C a chord step, L locks
  const b = $("sq-grid").children[i], p = sqPat(), s = p.st[i];
  b.className = "sqs" + (i >= p.steps ? " out" : "") + (s.note ? " has" : "") + (i === sq.s ? " sel" : "");
  b.textContent = "";
  el("small", {}, b, `${i + 1}${s.chord ? "C" : ""}${s.lmask ? "L" : ""}`);
  b.append(s.note ? noteName(s.note) : "OFF");
}
function sqChanged(i) { sq.dirty = true; if (i !== undefined) sqCell(i); sqStatus(); drawSqTabs(); }
function drawSqTabs() {
  const b = $("sq-tracks");
  b.textContent = "";
  for (let t = 0; t < 4; t++) {
    const p = sq.pats[t], n = p.st.slice(0, p.steps).filter((s) => s.note).length;
    const x = el("button", { cls: "trk" + (t === sq.t ? " on" : "") }, b);
    el("b", {}, x, `T${t + 1}`), x.append(` ${n} NOTES · ${p.steps} STEPS`);
    x.onclick = () => { sq.t = t; sq.s = 0; drawSeq(); };
  }
}
function drawSqTrack() {                                     // the track's own settings
  const box = $("sq-trk"), p = sqPat();
  box.textContent = "";
  el("h3", {}, box, `T${sq.t + 1}: PATTERN SETTINGS`);
  const g = el("div", { cls: "grid" }, box), a = el("div", {}, g), c = el("div", {}, g);
  control(a, "STEPS", 1, 64, () => p.steps, (v) => { p.steps = v; for (let i = 0; i < 64; i++) sqCell(i); sqChanged(); drawSqStep(); },
          (v) => `${v}`, true);
  choice(a, "DIVISION", DIV_NAMES, () => p.div, (v) => { p.div = v; sqChanged(); }, true);
  control(c, "SWING", 0, 127, () => p.swing, (v) => { p.swing = v; sqChanged(); }, (v) => `${Math.round(v * 100 / 127)}%`, true);
  choice(c, "PLAY MODE", MODE_NAMES, () => p.mode, (v) => { p.mode = v; sqChanged(); }, true);
  const r = el("div", { cls: "row" }, box);
  el("button", { onclick: () => {
    if (!confirm(`CLEAR ALL STEPS OF T${sq.t + 1} HERE?`)) return;
    p.st = Array.from({ length: SEQ.N }, emptyStep), sq.s = 0, sqChanged(), drawSeq();
  } }, r, `CLEAR T${sq.t + 1}`);
}
function drawSqGrid() {
  const g = $("sq-grid");
  g.textContent = "";
  for (let i = 0; i < SEQ.N; i++) {
    const b = el("button", { type: "button", "aria-label": `STEP ${i + 1}` }, g);
    b.onclick = () => sqSelect(i);
    sqCell(i);
  }
}
function drawSqStep() {                                      // the selected step
  const box = $("sq-step"), p = sqPat(), s = p.st[sq.s], inf = sqTrk(sq.t), eng = meta.engines[inf.eng];
  box.textContent = "";
  const hot = (fn) => (v) => { fn(v); sqChanged(sq.s); };
  const g = el("div", { cls: "grp" }, box);
  el("h3", {}, g, `T${sq.t + 1} · STEP ${sq.s + 1}` + (sq.s >= p.steps ? " (AFTER THE LAST STEP: NOT PLAYED)" : ""));
  control(g, "NOTE", 0, 127, () => s.note, hot((v) => { s.note = v; }), (v) => (v ? noteName(v) : "OFF"), true);
  control(g, "CHANCE", 0, 100, () => s.chance, hot((v) => { s.chance = v; }), (v) => `${v}%`, true);
  choice(g, "RATCHET", ["1 HIT", "2 HITS", "3 HITS", "4 HITS"], () => s.ratchet - 1, hot((v) => { s.ratchet = v + 1; }), true);
  control(g, "LENGTH", 1, 64, () => s.len, hot((v) => { s.len = v; }), (v) => `${v} ${v === 1 ? "STEP" : "STEPS"}`, true);
  const c = el("div", { cls: "grp" }, box);
  el("h3", {}, c, "CHORD STEP");
  const tg = el("button", { cls: "toggle" + (s.chord ? " on" : "") }, c, s.chord ? "CHORD STEP ON" : "CHORD STEP OFF");
  tg.onclick = () => { s.chord = s.chord ? 0 : 0x80; sqChanged(sq.s); drawSqStep(); };
  if (s.chord) {
    el("div", { cls: "ctl" }, c);
    choice(c, "CHORD TYPE", meta.chords, () => s.chord & 15, hot((v) => { s.chord = 0x80 | (s.chord & 0x70) | v; }), true);
    choice(c, "VOICING", meta.voicings, () => s.chord >> 4 & 7, hot((v) => { s.chord = 0x80 | (s.chord & 15) | v << 4; }), true);
  }
  const l = el("div", { cls: "grp" }, box);
  el("h3", {}, l, `STEP LOCKS (${eng.name}'S FOUR KNOBS)`);
  const vals = () => [0, 1, 2, 3].map((k) => (s.lmask >> k & 1 ? s.lv[k] : inf.k[k]));
  for (let k = 0; k < 4; k++) {
    const row = el("div", { cls: "lock" }, l), cb = el("input", { type: "checkbox", "aria-label": `LOCK ${eng.knobs[k]}` }, row);
    cb.checked = !!(s.lmask >> k & 1);
    const sl = control(row, eng.knobs[k], 0, eng.max[k], () => Math.min(eng.max[k], vals()[k]), hot((v) => {
      s.lv[k] = v, s.lmask |= 1 << k, cb.checked = true;
    }), (v) => (s.lmask >> k & 1 ? "" : "(TRACK) ") + vf.text("web_knob_text", inf.eng, ...vals().map((x, j) => (j === k ? v : x)), k), true);
    cb.onchange = () => {
      s.lmask = cb.checked ? s.lmask | 1 << k : s.lmask & ~(1 << k);
      if (cb.checked) s.lv[k] = Math.min(eng.max[k], inf.k[k]);
      sqChanged(sq.s), drawSqStep();
    };
  }
  const r = el("div", { cls: "row" }, box);
  el("button", { onclick: () => { p.st[sq.s] = emptyStep(); sqChanged(sq.s); drawSqStep(); } }, r, "CLEAR STEP");
  el("button", { onclick: () => { sq.clip = JSON.parse(JSON.stringify(s)); say(`STEP ${sq.s + 1} COPIED`, "ok"); } }, r, "COPY STEP");
  el("button", { onclick: () => {
    if (!sq.clip) { say("NOTHING COPIED YET", "err"); return; }
    p.st[sq.s] = JSON.parse(JSON.stringify(sq.clip)), sqChanged(sq.s), drawSqStep();
  } }, r, "PASTE STEP");
}
function drawSeq() {
  if (!meta) return;
  drawSqTabs(), drawSqTrack(), drawSqGrid(), drawSqStep(), sqStatus(), sqRecStatus();
}
const sqNeedsV2 = () => {
  if (conn.info.slotSize !== SEQ.SLOT_LEN) throw new EditorError("THIS FM-1'S SLOTS ARE NOT GR!MM 2.0'S: THE SEQUENCER NEEDS GR!MM 2.0", 0);
};
$("sq-import").onclick = () => job("IMPORT SLOT", async () => {
  sqNeedsV2();
  const k = +$("sq-slot").value, b = await conn.client.get(OBJ.SLOT + k, SEQ.SLOT_LEN);
  if (!b[0]) throw new EditorError(`SLOT ${"ABCD"[k]} IS EMPTY: NOTHING TO IMPORT`, 0);
  setSq(patternsFromSlot(b), [0, 1, 2, 3].map((t) => slotTrack(b, t)), `SLOT ${"ABCD"[k]} OF THE ${conn.kind === "virtual" ? "VIRTUAL " : ""}FM-1`, false);
  return `SLOT ${"ABCD"[k]} IMPORTED: THE FOUR PATTERNS ARE IN THE GRID`;
});
$("sq-now").onclick = () => job("IMPORT", async () => {
  sqNeedsV2();
  let w;
  try { w = await conn.client.get(OBJ.WORK, SEQ.WORK_LEN); } catch (e) {
    throw e.rc === 1 ? new EditorError("THE FM-1 DID NOT GIVE ITS PROJECT: TRY AGAIN IN A MOMENT (OR IT IS NOT GR!MM 2.0)", 1) : e;
  }
  setSq(patternsFromWork(w), [0, 1, 2, 3].map((t) => slotTrack(w, t, 0)), `WHAT THE ${conn.kind === "virtual" ? "VIRTUAL " : ""}FM-1 PLAYS NOW`, false);
  return "IMPORTED: THE FOUR PATTERNS THE FM-1 HAS NOW ARE IN THE GRID";
});
$("sq-send").onclick = () => {
  const k = +$("sq-slot").value, L = "ABCD"[k];
  if (!conn) { say("CONNECT FIRST (CONNECT)", "err"); return; }
  if (!confirm(`SEND THE FOUR PATTERNS TO SLOT ${L}?\n\nOnly the patterns (steps, settings, locks) change; the slot's sounds and mix stay. ` +
               "A backup of the slot is downloaded first, and the write is read back and checked.")) return;
  job(`SEND TO SLOT ${L}`, async () => {
    sqNeedsV2();
    say(`READING SLOT ${L}…`);
    let cur = await conn.client.get(OBJ.SLOT + k, SEQ.SLOT_LEN);
    const empty = !cur[0];
    if (empty) {                                             // (an empty slot: made of the FM-1's tracks as they are now)
      if (!confirm(`SLOT ${L} IS EMPTY. FILL IT WITH THE PATTERNS AND THE FM-1'S FOUR TRACKS AS THEY ARE NOW (SOUNDS AND MIX)?`)) return false;
      cur = slotFromWork(await conn.client.get(OBJ.WORK, SEQ.WORK_LEN), [0, 1, 2, 3].map(() => emptyPattern(16)));
    }
    const next = slotWithPatterns(cur, sq.pats);
    await conn.client.canWrite(OBJ.SLOT + k, next, conn.info.fmt);   // (refused while the FM-1 plays or sounds: before anything is done)
    const old = empty ? [0, 1, 2, 3].map(() => emptyPattern(16)) : patternsFromSlot(cur);
    download(`grimm-slot-${L}-backup-${stamp()}.json`, JSON.stringify(patternFile(old, { slot: L, was: empty ? "empty" : "used",
      firmware: conn.info.firmware, slotBytes: b64(cur) })));
    await conn.client.put(OBJ.SLOT + k, next, conn.info.fmt, progress);
    const back = await conn.client.get(OBJ.SLOT + k, SEQ.SLOT_LEN);
    if (back.some((x, i) => x !== next[i])) throw new EditorError(`SLOT ${L} DID NOT READ BACK AS SENT`, 0);
    const same = (a, b, from, to) => a.subarray(from, to).every((x, i) => x === b[from + i]);
    if (!same(cur, back, 0, SEQ.SLOT_SEQ) || !same(cur, back, SEQ.SLOT_END, SEQ.SLOT_LEN))
      throw new EditorError(`SLOT ${L}'S SOUNDS OR MIX CHANGED: RESTORE FROM THE BACKUP FILE`, 0);
    sq.dirty = false, sq.from = `SLOT ${L} OF THE ${conn.kind === "virtual" ? "VIRTUAL " : ""}FM-1`, sq.trk = [0, 1, 2, 3].map((t) => slotTrack(back, t));
    drawSeq();
    return `SLOT ${L} WRITTEN AND READ BACK (THE SOUNDS AND MIX AS THEY WERE; A BACKUP FILE IS DOWNLOADED). ON THE FM-1: HOLD SAVE + ${SQ_SLOT_KEY[k]}`;
  });
};
$("sq-save").onclick = () => {
  download(`grimm-patterns-${stamp()}.json`, JSON.stringify(patternFile(sq.pats)));
  say("PATTERNS SAVED TO A FILE (DOWNLOADED)", "ok");
};
$("sq-load").onclick = () => $("sq-file").click();
$("sq-file").onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = "";
  if (!f) return;
  try {
    setSq(parsePatternFile(JSON.parse(await f.text())), null, `FILE ${f.name.toUpperCase()}`, true);
    say(`${f.name.toUpperCase()} LOADED: THE FOUR PATTERNS ARE IN THE GRID (SEND TO A SLOT TO KEEP THEM ON THE FM-1)`, "ok");
  } catch (err) { say(`LOAD: ${errText(err)}`, "err"); }
};
$("sq-clear").onclick = () => {
  if (!confirm("CLEAR ALL FOUR PATTERNS HERE? (THE FM-1 KEEPS WHAT IT HAS UNTIL YOU SEND)")) return;
  setSq([0, 1, 2, 3].map(() => emptyPattern(16)), null, "EMPTY", false);
};

// the preview: the patterns into this page's own GR!MM (its slot, recalled, played), its sound as the SOUND EDITOR's
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
let engClient = null, engWait = [], engLeds = null;
function engReply(d) {                                       // a reply from the page's GR!MM (worklet.js): to the request waiting
  const k = engWait.findIndex((w) => w.cmd === d[4]);
  if (k < 0) return;
  const [w] = engWait.splice(k, 1);
  clearTimeout(w.timer), w.res(d.slice(5, d.length - 1));
}
const engTransport = {
  request(frame, timeout = 3000) {
    return new Promise((res, rej) => {
      if (!node) { rej(new EditorError("THE PAGE'S GR!MM IS NOT RUNNING (CLICK ANYWHERE, THEN TRY AGAIN)", 0)); return; }
      const w = { cmd: frame[3], res, timer: setTimeout(() => { engWait = engWait.filter((x) => x !== w); rej(new EditorError("no answer from this page's GR!MM", 97)); }, timeout) };
      engWait.push(w);
      node.port.postMessage({ type: "sysex", data: frame });
    });
  },
};
async function engRun(script) {                              // buttons and keys on the page's panel, one after the other
  for (const m of script) {
    if ("buttons" in m) node.port.postMessage({ type: "buttons", mask: m.buttons });
    if ("keys" in m) node.port.postMessage({ type: "keys", mask: m.keys });
    await sleep(m.ms);
  }
  node.port.postMessage({ type: "keys", mask: keyMask >>> 0 });   // (the keys held on this page's keyboard stay)
}
const enginePlays = () => !!(engLeds && engLeds[0] >>> BTN_PLAY & 1);
function sqPlayState() { $("sq-preview").dataset.state = enginePlays() ? "playing" : "stopped"; }
$("sq-preview").onclick = async () => {
  const b = $("sq-preview");
  b.disabled = true;
  try {
    await startEngine();
    if (!node) throw new EditorError("THE PAGE'S GR!MM IS NOT RUNNING", 0);
    const ec = engClient || (engClient = new Client(engTransport)), k = +$("sq-slot").value;
    say("PREVIEW: PREPARING THIS PAGE'S GR!MM…");
    node.port.postMessage({ type: "panic" });
    if (enginePlays()) await engRun(stopScript());
    const fmt = (await ec.info()).fmt, work = await ec.get(OBJ.WORK, SEQ.WORK_LEN);
    const slot = slotFromWork(work, sq.pats);                // (this page's tracks as the SOUND EDITOR has them, these patterns)
    for (let tries = 0; ; tries++) {
      try { await ec.put(OBJ.SLOT + k, slot, fmt); break; } catch (e) {
        if (e.rc !== 3 || tries >= 60) throw e;
        say("PREVIEW: WAITING FOR THIS PAGE'S GR!MM TO FALL SILENT…");
        await sleep(500);
      }
    }
    await engRun(recallScript(k, true));
    await sleep(200);
    sqPlayState();
    say("PREVIEW: PLAYING IN THIS PAGE, WITH THE SOUNDS OF T1–T4 AS IN THE SOUND EDITOR (STOP ENDS IT)", "ok");
  } catch (e) { say(`PREVIEW: ${errText(e)}`, "err"); } finally { b.disabled = false; sqPlayState(); }
};
$("sq-stop").onclick = async () => {
  if (!node) return;
  node.port.postMessage({ type: "panic" });
  if (enginePlays()) await engRun(stopScript());
  await sleep(200);
  sqPlayState();
  say("PREVIEW STOPPED", "ok");
};

// ------------------------------------------------------------------ STEP RECORD: a MIDI keyboard or the computer's keys write the notes
// REC armed: a note goes into the selected step of the selected track and the cursor moves on (wraps at STEPS); REST, the sustain
// pedal and Space skip a step (OFF), TIE makes the step before one longer; 3+ keys pressed together that are one of the FM-1's
// chords (any VOICING) make a chord step, else the lowest note is written. It writes only while this section is shown.
const SQ_MIDI_KEY = "grimm-seq-midi";
const FM1_PORT = /felucca|fm-?1|m-?vave|gr!?mm/i;            // (the FM-1's own port: not a keyboard)
const sqOn = () => $("s-sequencer").classList.contains("on");
const sqTyping = () => {                                     // a text field has the keys: never taken
  const a = document.activeElement;
  return !!a && (a.tagName === "TEXTAREA" || a.tagName === "SELECT" || (a.tagName === "INPUT" && !/^(range|checkbox|button)$/.test(a.type)));
};
const sqRecOn = () => sq.rec && sqOn();
function sqRecMsg(text, bad = false) { const m = $("sq-recmsg"); m.textContent = text; m.className = "px" + (bad ? " bad" : ""); }
function sqRecStatus() {
  const p = sqPat();
  $("sq-rec").textContent = sq.rec ? "REC ON" : "REC", $("sq-rec").classList.toggle("on", sq.rec);
  $("sq-grid").classList.toggle("rec", sq.rec);
  $("sq-oct").textContent = `OCT ${sq.oct}`;
  $("sq-recst").textContent = sq.rec ? `REC: T${sq.t + 1} · STEP ${sq.s + 1} OF ${p.steps}` : "";
}
function sqSelect(i) {                                       // the cursor
  const old = sq.s;
  sq.s = i, sqCell(old), sqCell(i), drawSqStep(), sqRecStatus();
  const c = $("sq-grid").children[i];
  if (sq.rec && c && c.scrollIntoView) c.scrollIntoView({ block: "nearest" });
}
const noteLabel = (n) => noteName(n);
function sqCommit(notes) {                                   // keys pressed together (within 50 ms)
  if (!sqRecOn()) return;
  const r = recWrite(sqPat(), sq.s, notes);
  if (!r) return;
  sqChanged(r.wrote);
  const where = `T${sq.t + 1} STEP ${r.wrote + 1}`;
  if (r.kind === "chord") sqRecMsg(`${where}: ${noteLabel(r.note)} ${meta.chords[r.chord & 15]} ${meta.voicings[r.chord >> 4 & 7]} (CHORD STEP)`);
  else if (r.kind === "nomatch") sqRecMsg("CHORD NOT ON THE FM-1: THE LOWEST NOTE IS WRITTEN (" + noteLabel(r.note) + ")", true);
  else if (r.kind === "pair") sqRecMsg(`${where}: 2 KEYS: THE LOWEST NOTE, ${noteLabel(r.note)}`);
  else sqRecMsg(`${where}: ${noteLabel(r.note)}`);
  sqSelect(r.cursor);
}
const sqGroup = makeGrouper(sqCommit, 50);
function engMidi(st, n, v) { if (node) node.port.postMessage({ type: "midi", data: [st | sq.t, n, v] }); }   // (the page's GR!MM plays it: the track's sound)
function sqNoteOn(n) {
  if (sq.mon) engMidi(0x90, n, 100);
  if (sqRecOn()) sqGroup.add(n);
}
const sqNoteOff = (n) => { if (sq.mon) engMidi(0x80, n, 0); };
function sqRest() {
  const r = recRest(sqPat(), sq.s);
  sqChanged(r.wrote), sqRecMsg(`T${sq.t + 1} STEP ${r.wrote + 1}: REST`);
  sqSelect(r.cursor);
}
function sqTie() {
  const r = recTie(sqPat(), sq.s);
  if (!r) { sqRecMsg("NOTHING TO TIE: THE STEP BEFORE HAS NO NOTE", true); return; }
  sqChanged(r.tied), sqRecMsg(`T${sq.t + 1} STEP ${r.tied + 1} TIED: ${r.len} ${r.len === 1 ? "STEP" : "STEPS"} LONG`);
  if (r.tied === sq.s) drawSqStep();
}
$("sq-rec").onclick = (e) => {
  sq.rec = !sq.rec;
  if (!sq.rec) { sqRecMsg("REC OFF"); node && node.port.postMessage({ type: "panic" }); } else sqRecMsg("REC ON: PLAY A KEY (MIDI KEYBOARD, OR A W S E D F T G Y H U J K ON THE COMPUTER)");
  sqRecStatus();
  e.currentTarget.blur();                                    // (Space is a REST in REC: not a click on this button)
};
$("sq-rest").onclick = sqRest;
$("sq-tie").onclick = sqTie;
$("sq-mon").onclick = () => { sq.mon = !sq.mon, $("sq-mon").classList.toggle("on", sq.mon); if (!sq.mon && node) node.port.postMessage({ type: "panic" }); };
$("sq-octd").onclick = () => { sq.oct = Math.max(0, sq.oct - 1), sqRecStatus(); };
$("sq-octu").onclick = () => { sq.oct = Math.min(8, sq.oct + 1), sqRecStatus(); };
// the computer's keys (REC armed, this section shown, never in a text field)
const sqDown = new Map();                                    // key -> the note it sounded
addEventListener("keydown", (e) => {
  if (!sqRecOn() || e.ctrlKey || e.metaKey || e.altKey || sqTyping()) return;
  const k = e.key.length === 1 ? e.key.toLowerCase() : e.key, n = pcKeyNote(k, sq.oct), cols = getComputedStyle($("sq-grid")).gridTemplateColumns.split(" ").length;
  const onRange = document.activeElement && document.activeElement.type === "range";
  let used = true;
  if (n >= 0) { if (!e.repeat) sqDown.set(k, n), sqNoteOn(n); }
  else if (k === "z") sq.oct = Math.max(0, sq.oct - 1), sqRecStatus();
  else if (k === "x") sq.oct = Math.min(8, sq.oct + 1), sqRecStatus();
  else if ((k === "ArrowLeft" || k === "ArrowRight") && !onRange) {   // (the cursor wraps at the track's STEPS)
    const n = sqPat().steps;
    sqSelect((((sq.s + (k === "ArrowRight" ? 1 : -1)) % n) + n) % n);
  } else if (k === "ArrowUp" || k === "ArrowDown") sqSelect(Math.max(0, Math.min(sqPat().steps - 1, sq.s + (k === "ArrowDown" ? cols : -cols))));
  else if (k === "Backspace") { sqPat().st[sq.s] = emptyStep(); sqChanged(sq.s), drawSqStep(), sqRecMsg(`T${sq.t + 1} STEP ${sq.s + 1}: CLEARED`); }
  else if (k === " ") { if (!e.repeat) sqRest(); }
  else used = false;
  if (used) e.preventDefault();
}, true);
addEventListener("keyup", (e) => {
  const k = e.key.length === 1 ? e.key.toLowerCase() : e.key;
  if (sqDown.has(k)) { sqNoteOff(sqDown.get(k)), sqDown.delete(k); }
  if (k === " " && sqRecOn() && !sqTyping()) e.preventDefault();   // (not a click on a button that has the focus)
}, true);
addEventListener("blur", () => { sqDown.forEach((n) => sqNoteOff(n)); sqDown.clear(); });
// MIDI in: Web MIDI, any input but the FM-1's own; the choice kept in this browser
let sqMidiAcc = null, sqMidiIn = null;
const savedMidi = () => { try { return localStorage.getItem(SQ_MIDI_KEY) || ""; } catch { return ""; } };
const saveMidi = (v) => { try { localStorage.setItem(SQ_MIDI_KEY, v); } catch { /* (no storage: not kept) */ } };
function sqMidiMsg(e) {
  const [st, d1, d2] = e.data, c = st & 0xf0;
  if (c === 0x90 && d2 > 0) sqNoteOn(d1);
  else if (c === 0x80 || (c === 0x90 && d2 === 0)) sqNoteOff(d1);
  else if (c === 0xb0 && d1 === 64) {                        // the sustain pedal: down = a REST
    const down = d2 >= 64;
    if (down && !sq.pedal && sqRecOn()) sqRest();
    sq.pedal = down;
  }
}
function sqMidiList() {
  if (!sqMidiAcc) return;
  const sel = $("sq-midi"), own = conn && conn.kind === "fm1" ? conn.transport.input.name : "", want = sel.value || savedMidi();
  const ins = [...sqMidiAcc.inputs.values()].filter((i) => !FM1_PORT.test(i.name || "") && i.name !== own);
  sel.textContent = "";
  el("option", { value: "" }, sel, "OFF");
  ins.forEach((i) => el("option", { value: i.name }, sel, i.name));
  sel.value = ins.some((i) => i.name === want) ? want : "";
  if (sqMidiIn) sqMidiIn.onmidimessage = null, sqMidiIn = null;
  sqMidiIn = ins.find((i) => i.name === sel.value) || null;
  if (sqMidiIn) sqMidiIn.onmidimessage = sqMidiMsg;
  $("sq-midi-n").textContent = ins.length ? `${ins.length} FOUND` : "NONE FOUND";
}
async function sqMidiInit(ask) {
  if (!navigator.requestMIDIAccess) { if (ask) sqRecMsg("THIS BROWSER HAS NO WEB MIDI: USE CHROME OR EDGE (THE COMPUTER'S KEYS STILL WORK)", true); return; }
  try {
    sqMidiAcc = sqMidiAcc || await navigator.requestMIDIAccess({ sysex: false });
    sqMidiAcc.onstatechange = sqMidiList;
    sqMidiList();
  } catch (e) { if (ask) sqRecMsg(`MIDI: ${errText(e)}`, true); }
}
$("sq-midi-scan").onclick = () => sqMidiInit(true);
$("sq-midi").onchange = () => { saveMidi($("sq-midi").value), sqMidiList(); };
if (savedMidi()) sqMidiInit(false);                          // (a keyboard chosen before: found again, no question if allowed already)

// ------------------------------------------------------------------ start: the four tracks as GR!MM starts
(async () => {
  try {
    wasm = await (await fetch("grimm.wasm")).arrayBuffer();
    vf = await VirtualFM1.create(wasm.slice(0));
    vf.log = midiLog;
    vfc = new Client(vf);
    meta = vf.meta();
    for (let t = 0; t < 4; t++) {
      const r = await vfc.track(t, true);
      tracks[t] = fromDevice(r.rec, t), label[t] = r.name, labelEng[t] = tracks[t].eng;
      loaded[t] = JSON.stringify(tracks[t]), from[t] = "AS GR!MM STARTS";
    }
  } catch (e) {
    say(`COULD NOT LOAD GR!MM (grimm.wasm): ${e.message || e}. OPEN THIS PAGE FROM A WEB SERVER, NOT A FILE.`.toUpperCase(), "err");
    return;
  }
  screenText(["CLICK ANYWHERE:", "GR!MM'S SOUND", "AND SCREEN"]);
  startEngine();
  drawKeys();
  drawSlots();
  drawTracks();
  drawEditor();
  drawSeq();
  drawSamples();
  setConn(null);
  say("READY: CONNECT THE FM-1, OR TRY WITHOUT ONE. THE EDITOR WORKS HERE ALREADY.");
  const q = new URLSearchParams(location.search), h = location.hash.slice(1);
  show(h || (q.has("fact") ? "editor" : "connect"));
  if (q.has("fact")) { pick = { fact: true, i: +q.get("fact") || 0 }; drawSlots(); loadTo(0); }
  if (q.has("try")) await tryVirtual();
  if (h) show(h);
  if (h === "samples") await sampleInfo();
})();
