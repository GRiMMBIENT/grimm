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
         sampleName, noteName, pack7, hex } from "./editor_core.js";

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
}
document.querySelectorAll("nav a").forEach((a) => a.addEventListener("click", () => { show(a.dataset.s); history.replaceState(null, "", "#" + a.dataset.s); }));

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
        else if (m.type === "frame" && m.fb) drawScreen(m.fb);
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
  const s = $("conn");
  s.textContent = !c ? "NOT CONNECTED" : c.kind === "virtual" ? `VIRTUAL FM-1 · GR!MM ${c.info.firmware}` : `FM-1 · GR!MM ${c.info.firmware}`;
  s.classList.toggle("on", !!c);
  const t = $("info");
  t.textContent = "";
  if (c) {
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
    if (!p) throw new EditorError("NO FM-1 WITH GR!MM 26A OR LATER FOUND (USB DATA CABLE? ANOTHER APP ON ITS MIDI PORT?) SEE THE MIDI LOG", 0);
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
function slotLabel(r, i, fact) {
  const n = String(i + 1).padStart(2, "0");
  if (!fact && isEmpty(r)) return [n, "— EMPTY —"];
  const s = parseSound(r, meta), name = fact ? meta.factory[i].name : soundTitle(r, meta);
  return [n, `${name} (${meta.engines[s.eng].name})`];
}
const pickRec = () => (pick.fact ? Uint8Array.from(meta.factory[pick.i].rec) : user[pick.i]);
const pickName = () => (pick.fact ? `FACTORY ${pick.i + 1}: ${meta.factory[pick.i].name}` :
  `SLOT ${String(pick.i + 1).padStart(2, "0")}` + (isEmpty(user[pick.i]) ? " (EMPTY)" : `: ${soundTitle(user[pick.i], meta)}`));
function drawSlots() {
  const u = $("user"), f = $("factory");
  u.textContent = f.textContent = "";
  user.forEach((r, i) => {
    const [n, t] = slotLabel(r, i, false), on = !pick.fact && pick.i === i;
    const d = el("div", { cls: "slot" + (isEmpty(r) ? " empty" : "") + (on ? " sel" : "") + (dirty[i] ? " dirty" : ""),
                          onclick: () => { pick = { fact: false, i }; drawSlots(); }, ondblclick: () => loadTo(selT) }, u);
    el("span", { cls: "n" }, d, n + " "), d.append(t);
  });
  meta.factory.forEach((x, i) => {
    const [n, t] = slotLabel(Uint8Array.from(x.rec), i, true);
    const d = el("div", { cls: "slot" + (pick.fact && pick.i === i ? " sel" : ""), onclick: () => { pick = { fact: true, i }; drawSlots(); },
                          ondblclick: () => loadTo(selT) }, f);
    el("span", { cls: "n" }, d, n + " "), d.append(t);
  });
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
  ["b-readall", "b-store", "b-backup", "b-restore", "b-sinfo", "b-sfactory"].forEach((id) => { $(id).disabled = !c; });
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
function control(parent, label, min, max, get, set, text) {
  const c = el("div", { cls: "ctl" }, parent), lab = el("div", { cls: "lab" }, c), v = el("span");
  el("label", {}, lab, label), lab.appendChild(v);
  const r = el("input", { type: "range", min, max, step: 1 }, c);
  r.value = get();
  const upd = () => { v.textContent = text ? text(+r.value) : String(r.value); };
  r.addEventListener("input", () => { set(+r.value); upd(); changed(); });
  upd();
  return r;
}
function choice(parent, label, names, get, set) {
  const c = el("div", { cls: "ctl" }, parent), lab = el("div", { cls: "lab" }, c);
  el("label", {}, lab, label);
  const s = el("select", {}, c);
  s.style.width = "100%";
  names.forEach((n, i) => el("option", { value: i }, s, n));
  s.value = get();
  s.addEventListener("change", () => { set(+s.value); changed(); });
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
  const t = el("table", { cls: "ops" }, f), h = el("tr", {}, t);
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
  if (k === undefined || e.repeat || e.ctrlKey || e.metaKey || e.altKey || /INPUT|SELECT|TEXTAREA/.test(document.activeElement.tagName) &&
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
      const sl = makeSlot(buf, sampleName(nm.value), Math.max(0, Math.min(127, +root.value | 0)));
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
  return "BACKUP MADE: ALL 64 SOUNDS, SLOTS A–D, THE SETTINGS (DOWNLOADED)";
});
$("b-restore").onclick = () => $("f-restore").click();
$("f-restore").onchange = async (e) => {
  const f = e.target.files[0];
  e.target.value = "";
  if (!f) return;
  let objs;
  try { objs = checkBackup(JSON.parse(await f.text()), conn.info); } catch (err) { say(`RESTORE: ${errText(err)} (NOTHING WRITTEN)`, "err"); return; }
  if (!confirm(`RESTORE ${f.name}?\n\nThis REPLACES all 64 user sounds, slots A–D and the settings on the FM-1. A backup of what it holds now is made (downloaded) first.`)) return;
  await job("RESTORE", async () => {
    const before = await makeBackup(conn.client, conn.info, (i, n) => { progress(i, n); say("FIRST, A BACKUP OF WHAT THE FM-1 HOLDS NOW…"); });
    download(`grimm-backup-before-restore-${stamp()}.json`, JSON.stringify(before, null, 1));
    await restoreBackup(conn.client, objs, conn.info, (i, n, what) => { progress(i, n); say(`WRITING ${what.toUpperCase()}…`); });
    for (let i = 0; i < objs.length; i++) {                  // read back: as written
      const d = await conn.client.get(objs[i].id, objs[i].data.length);
      if (d.some((x, k) => x !== objs[i].data[k])) throw new EditorError(`${objs[i].what.toUpperCase()} DID NOT READ BACK AS WRITTEN`, 0);
      progress(i + 1, objs.length);
    }
    for (const o of objs) if (o.id >= OBJ.SND && o.id < OBJ.SND + 64) { user[o.id - OBJ.SND] = o.data; dirty[o.id - OBJ.SND] = false; }
    drawSlots();
    return "RESTORED AND READ BACK: EVERYTHING AS IN THE FILE (THE BACKUP FROM BEFORE IS DOWNLOADED)";
  });
};

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
