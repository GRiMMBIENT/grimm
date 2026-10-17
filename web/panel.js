// SPDX-License-Identifier: GPL-3.0-only
// Copyright (C) 2026 GR!MM
// After Felucca's browser page, web/emu/index.html of hugelton/Felucca (Copyright (C) 2026 Leo Kuroshita
// (@kurogedelic), Hügelton Instruments), which follows X0X's (charlesvestal/fm1-x0x), both GPL-3.0: the panel's
// layout, the computer keyboard's map, the screen and lights from the worklet, Web MIDI in.
// The page of GR!MM in the browser: the FM-1's panel, its input to the worklet (worklet.js), the screen and the
// lights back, the settings kept in this browser.

// ids as platform.c names them: buttons BTN_* (bit = id), encoders ENC_*, keys bit k = key k (0 = F3 .. 26 = G5)
const BTN = { "OCT-": 0, "OCT+": 1, FX: 2, SCL: 3, ENV: 4, LFO: 5, EDIT: 6, GLO: 7, HOME: 8, SAVE: 9, ARP: 10, SEQ: 11, PLAY: 12, REC: 13 };
const BTN_NAME = { SCL: "BTN_SEL" };                       // (platform.c's name where it differs from the print)
const ENC = { SELECT: 0, ALGORITHM: 1, "KNOB 1": 2, "KNOB 2": 3, "KNOB 3": 4, "KNOB 4": 5, PRESETS: 6 };
const ROW1 = ["FX", "SCL", "ENV", "LFO", "EDIT", "GLO"], ROW2 = ["HOME", "SAVE", "ARP", "SEQ", "PLAY", "REC"];
const WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26];
const BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25];
const BLACK_AFTER = [0, 1, 2, 4, 5, 7, 8, 9, 11, 12, 14];  // the white key each black one follows
const NOTE = ["F", "F♯", "G", "G♯", "A", "A♯", "B", "C", "C♯", "D", "D♯", "E"];
const keyName = (k) => NOTE[k % 12] + (3 + Math.floor((k + 5) / 12));
const STORE = "grimm-web-settings";

let node = null, btnMask = 0, keyMask = 0, latched = 0, master = 700;
const send = (m) => { if (node) node.port.postMessage(m); };
const btnBy = new Map(), keyBy = new Map();                // pointer / computer key -> button id or key
const buttons = [], keys = [], knobs = [];
const $ = (id) => document.getElementById(id);
function make(tag, cls, parent, text) {
  const e = document.createElement(tag);
  if (cls) e.className = cls;
  if (text) e.textContent = text;
  parent.appendChild(e);
  return e;
}

// ------------------------------------------------------------------ the panel
// In mm on the FM-1's 161.5 x 96.5 mm face. The positions and sizes are Felucca's drawing of the FM-1 (web/emu,
// its "wide" layout, 1000 x 624 units) put on the real outline: x and every size x 0.1615 mm, y x 96.5 / 624.
// The screen (240 x 240) is 24 % of the panel's width, as in Felucca's and JIANT FM's pages.
const W = 161.5, H = 96.5, UX = 0.1615, UY = 96.5 / 624;
const L = {
  knobs: { MASTER: [105, 100, 24], SELECT: [215, 100, 24], PRESETS: [105, 205, 24], ALGORITHM: [215, 205, 24],
           "KNOB 1": [610, 100, 22], "KNOB 2": [714, 100, 22], "KNOB 3": [818, 100, 22], "KNOB 4": [922, 100, 22] },
  oct: { "OCT-": [60, 258, 90, 34], "OCT+": [170, 258, 90, 34] },
  grid: [582, 174, 54, 48, 63, 62],                        // the 2 x 6 buttons: x, y, w, h, step x, step y
  bezel: [292, 44, 256], keybed: [36, 332, 928, 256],
};
const SHORT = {};                                          // the computer key of each key / button, for the tooltips
"Z X C V B N M , . /".split(" ").forEach((c, i) => SHORT["k" + WHITE[i]] = c);
"S D F H J L ; '".split(" ").forEach((c, i) => SHORT["k" + [1, 3, 5, 8, 10, 13, 15, 17][i]] = c);
"Q W E R T Y U I O".split(" ").forEach((c, i) => SHORT["k" + (18 + i)] = c);
"1 2 3 4 5 6 7 8 9 0 - =".split(" ").forEach((c, i) => SHORT[[...ROW1, ...ROW2][i]] = c);
Object.assign(SHORT, { "OCT-": "[", "OCT+": "]" });
const fm1 = $("fm1");
function place(el, x, y, w, h) {                           // mm -> percentages of the panel
  Object.assign(el.style, { left: `${x / W * 100}%`, top: `${y / H * 100}%`, width: `${w / W * 100}%`, height: `${h / H * 100}%` });
  return el;
}
const at = (tag, cls, x, y, w, h, text) => place(fm1.insertBefore(make(tag, cls, fm1, text), $("start")), x, y, w, h);

function knob(name) {
  const [ux, uy, ur] = L.knobs[name], x = ux * UX, y = uy * UY, r = (ur + 5) * UX;   // (the cap and its collar)
  const k = { name, enc: ENC[name], angle: name === "MASTER" ? -135 + 270 * master / 1023 : 0 };
  at("div", "label", x - 8, y - r - 4.2, 16, 3.4, name);   // (16 mm: under the knobs' 16.8 mm spacing)
  k.dial = at("div", "dial", x - r, y - r, 2 * r, 2 * r);
  k.dial.tabIndex = 0;
  k.dial.setAttribute("role", "slider");
  k.dial.setAttribute("aria-label", name);
  k.dial.title = name === "MASTER" ? "MASTER: the volume" : `${name} (ENC_${name.replace(" ", "")})` +
    ({ SELECT: ": ↑ ↓", ALGORITHM: ": ← →" }[name] || "");
  knobs.push(k);
  bindKnob(k);
  drawKnob(k);
}
function button(name, [ux, uy, uw, uh]) {
  const b = { name, id: BTN[name] };
  b.el = at("button", "btn", ux * UX, uy * UY, uw * UX, uh * UX);
  b.el.type = "button";
  b.el.innerHTML = (name === "PLAY" ? "PLAY<br>STOP" : name) + `<span class="sc">${SHORT[name]}</span>`;   // (its computer key)
  b.el.title = `${name} (${BTN_NAME[name] || "BTN_" + name.replace("OCT-", "OCT_DOWN").replace("OCT+", "OCT_UP")}): ${SHORT[name]}`;
  buttons.push(b);
  bindButton(b);
}
function key(k, cx, cy, kw, kh) {
  const el = at("div", "key", cx - kw / 2, cy - kh / 2, kw, kh);
  el.setAttribute("role", "button");
  el.setAttribute("aria-label", keyName(k));
  el.title = `${keyName(k)}: ${SHORT["k" + k]}`;
  keys[k] = { el };
  bindKey(el, k);
}
{
  const [bx, by, bs] = L.bezel, [x0, y0, w, h] = L.keybed;
  at("div", "bezel", bx * UX, by * UY, bs * UX, bs * UX);
  at("div", "bed", x0 * UX, y0 * UY, w * UX, h * UX);
  Object.keys(L.knobs).forEach(knob);
  Object.entries(L.oct).forEach(([n, r]) => button(n, r));
  const [gx, gy, gw, gh, dx, dy] = L.grid;
  ROW1.forEach((n, i) => button(n, [gx + i * dx, gy, gw, gh]));
  ROW2.forEach((n, i) => button(n, [gx + i * dx, gy + dy, gw, gh]));
  // the keys: two rows, the sharps above, each key a capsule (Felucca's proportions: 0.78 of the pitch wide, 0.425 of the bed high)
  const pitch = (w - 24) / 16 * UX, kw = pitch * 0.78, kh = h * 0.425 * UX, left = (x0 + 12) * UX;
  const top = y0 * UY, bedH = h * UX;
  WHITE.forEach((k, i) => key(k, left + (i + 0.5) * pitch, top + bedH * 0.75, kw, kh));
  BLACK.forEach((k, j) => key(k, left + (BLACK_AFTER[j] + 1) * pitch, top + bedH * 0.25, kw, kh));
}

// the screen: its real size on the panel (SCREEN 1x), or twice that over the panel to see the pixels (2x)
const canvas = $("screen");
let zoom = 1;
function placeScreen() {
  const [bx, by, bs] = L.bezel, inset = bs / 32 * UX, s = (bs * UX - 2 * inset) * zoom, cx = (bx + bs / 2) * UX;
  place(canvas, cx - s / 2, by * UY + inset, s, s);
  canvas.classList.toggle("big", zoom > 1);
}
placeScreen();
$("zoom").addEventListener("click", () => {
  zoom = zoom === 1 ? 2 : 1;
  placeScreen();
  $("zoom").setAttribute("aria-pressed", zoom > 1);
  for (const b of $("zoom").querySelectorAll("b")) b.classList.toggle("on", +b.dataset.s === zoom);
});

// ------------------------------------------------------------------ input
function syncButtons() {
  let m = latched;
  for (const id of btnBy.values()) m |= 1 << id;
  if (m !== btnMask) { btnMask = m; send({ type: "buttons", mask: m }); }
  for (const b of buttons) {
    b.el.classList.toggle("held", !!(m & (1 << b.id)) && !(latched & (1 << b.id)));
    b.el.classList.toggle("latched", !!(latched & (1 << b.id)));
  }
}
function syncKeys() {
  let m = 0;
  for (const k of keyBy.values()) m |= 1 << k;
  if (m !== keyMask) { keyMask = m; send({ type: "keys", mask: m }); }
  keys.forEach((k, i) => k.el.classList.toggle("held", !!(m & (1 << i))));
}
function bindButton(b) {
  b.el.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    if (e.shiftKey || e.button === 2) { latched ^= 1 << b.id; syncButtons(); return; }   // Shift-click: held
    if (latched & (1 << b.id)) { latched &= ~(1 << b.id); syncButtons(); return; }
    b.el.setPointerCapture(e.pointerId);
    btnBy.set("p" + e.pointerId, b.id);
    syncButtons();
  });
  b.el.addEventListener("keydown", (e) => {
    if ((e.key === "Enter" || e.key === " ") && !e.repeat) { btnBy.set("f" + b.id, b.id); syncButtons(); e.preventDefault(); e.stopPropagation(); }
  });
  b.el.addEventListener("keyup", () => { if (btnBy.delete("f" + b.id)) syncButtons(); });
  b.el.addEventListener("contextmenu", (e) => e.preventDefault());
}
function bindKey(el, k) {
  el.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    el.setPointerCapture(e.pointerId);
    keyBy.set("p" + e.pointerId, k);
    syncKeys();
  });
  el.addEventListener("contextmenu", (e) => e.preventDefault());
}
function release(e) {
  const id = "p" + e.pointerId;
  if (btnBy.delete(id)) syncButtons();
  if (keyBy.delete(id)) syncKeys();
}
for (const t of ["pointerup", "pointercancel", "lostpointercapture"]) $("fm1").addEventListener(t, release);

function drawKnob(k) { k.dial.style.setProperty("--a", `${k.angle}deg`); }
function turn(k, n) {
  if (!n) return;
  if (k.name === "MASTER") {                               // a pot, 0..1023 as the ADC reads it
    master = Math.max(0, Math.min(1023, master + n * 16));
    k.angle = -135 + 270 * master / 1023;
    k.dial.setAttribute("aria-valuenow", master);
    send({ type: "master", value: master });
  } else {                                                 // an encoder: steps
    k.angle = (k.angle + n * 18) % 360;
    send({ type: "enc", enc: k.enc, n });
  }
  drawKnob(k);
}
function bindKnob(k) {
  let drag = null, wheel = 0;
  k.dial.addEventListener("pointerdown", (e) => {
    e.preventDefault();
    k.dial.setPointerCapture(e.pointerId);
    k.dial.focus();
    drag = { y: e.clientY, acc: 0 };
  });
  k.dial.addEventListener("pointermove", (e) => {
    if (!drag) return;
    drag.acc += drag.y - e.clientY;
    drag.y = e.clientY;
    const step = k.name === "MASTER" ? 3 : 8, n = Math.trunc(drag.acc / step);
    if (n) { drag.acc -= n * step; turn(k, n); }
  });
  for (const t of ["pointerup", "pointercancel"]) k.dial.addEventListener(t, () => { drag = null; });
  k.dial.addEventListener("wheel", (e) => {
    e.preventDefault();
    const dy = e.deltaMode === 1 ? e.deltaY * 33 : e.deltaY;
    if (Math.abs(dy) >= 50) { turn(k, dy < 0 ? 1 : -1); wheel = 0; return; }   // a wheel notch: one step
    wheel -= dy;                                                                   // a trackpad: by distance
    const n = Math.trunc(wheel / 25);
    if (n) { wheel -= n * 25; turn(k, n); }
  }, { passive: false });
  k.dial.addEventListener("keydown", (e) => {
    const d = { ArrowUp: 1, ArrowRight: 1, ArrowDown: -1, ArrowLeft: -1 }[e.key];
    if (d) { turn(k, d); e.preventDefault(); e.stopPropagation(); }
  });
}

// the computer keyboard (Felucca's map): F3..A♯4 on the Z (white) and A (black) rows, B4..G5 on Q..O in semitones,
// the twelve buttons on the number row in the panel's order, [ ] OCT, the arrows SELECT and ALGORITHM
const KEYMAP = {};
["KeyZ", "KeyX", "KeyC", "KeyV", "KeyB", "KeyN", "KeyM", "Comma", "Period", "Slash"].forEach((c, i) => KEYMAP[c] = { key: WHITE[i] });
[["KeyS", 1], ["KeyD", 3], ["KeyF", 5], ["KeyH", 8], ["KeyJ", 10], ["KeyL", 13], ["Semicolon", 15], ["Quote", 17]].forEach(([c, k]) => KEYMAP[c] = { key: k });
["KeyQ", "KeyW", "KeyE", "KeyR", "KeyT", "KeyY", "KeyU", "KeyI", "KeyO"].forEach((c, i) => KEYMAP[c] = { key: 18 + i });
["Digit1", "Digit2", "Digit3", "Digit4", "Digit5", "Digit6", "Digit7", "Digit8", "Digit9", "Digit0", "Minus", "Equal"]
  .forEach((c, i) => KEYMAP[c] = { btn: BTN[[...ROW1, ...ROW2][i]] });
Object.assign(KEYMAP, { BracketLeft: { btn: BTN["OCT-"] }, BracketRight: { btn: BTN["OCT+"] } });
const ARROWS = { ArrowUp: ["SELECT", 1], ArrowDown: ["SELECT", -1], ArrowRight: ["ALGORITHM", 1], ArrowLeft: ["ALGORITHM", -1] };
addEventListener("keydown", (e) => {                       // (a knob in focus takes only its arrows: the rest plays on, so a
  if (!node || e.metaKey || e.ctrlKey || e.altKey) return;  // button's key held + a key works after a knob was turned)
  if (ARROWS[e.code]) { const [n, d] = ARROWS[e.code]; turn(knobs.find((k) => k.name === n), d); e.preventDefault(); return; }
  const m = KEYMAP[e.code];
  if (!m) return;
  e.preventDefault();
  if (e.repeat) return;
  if (m.key !== undefined) { keyBy.set("k" + e.code, m.key); syncKeys(); }
  else { btnBy.set("k" + e.code, m.btn); syncButtons(); }
});
addEventListener("keyup", (e) => {
  const id = "k" + e.code;
  if (keyBy.delete(id)) syncKeys();
  if (btnBy.delete(id)) syncButtons();
});
addEventListener("blur", () => {                           // keys held when the window lost focus: up
  for (const id of [...btnBy.keys()]) if (id[0] !== "p") btnBy.delete(id);
  for (const id of [...keyBy.keys()]) if (id[0] !== "p") keyBy.delete(id);
  syncButtons();
  syncKeys();
});

// ------------------------------------------------------------------ the screen and the lights
const ctx = $("screen").getContext("2d");
ctx.imageSmoothingEnabled = false;
const img = ctx.createImageData(240, 240);
function drawScreen(fb) {                                  // RGB565
  const d = img.data;
  for (let i = 0; i < 240 * 240; i++) {
    const p = fb[i];
    d[i * 4] = ((p >> 11) & 31) * 255 / 31;
    d[i * 4 + 1] = ((p >> 5) & 63) * 255 / 63;
    d[i * 4 + 2] = (p & 31) * 255 / 31;
    d[i * 4 + 3] = 255;
  }
  ctx.putImageData(img, 0, 0);
}
function drawLights([litB, litK]) {
  for (const b of buttons) b.el.classList.toggle("lit", !!(litB & (1 << b.id)));
  keys.forEach((k, i) => k.el.classList.toggle("lit", !!(litK & (1 << i))));
}

// ------------------------------------------------------------------ the settings, kept in this browser
function loadSettings() {
  try {
    const a = JSON.parse(localStorage.getItem(STORE) || "null");
    return Array.isArray(a) ? Uint8Array.from(a.slice(0, 256)) : null;
  } catch { return null; }
}
function saveSettings(u8) {
  try { localStorage.setItem(STORE, JSON.stringify([...u8])); return true; } catch { return false; }
}

// ------------------------------------------------------------------ Web MIDI in (channel messages)
const status = $("status");
async function midiOn() {
  if (!navigator.requestMIDIAccess) { status.textContent = "MIDI in: this browser has no Web MIDI."; return; }
  try {
    const access = await navigator.requestMIDIAccess();
    const bind = () => {
      const names = [];
      for (const input of access.inputs.values()) {
        names.push(input.name);
        input.onmidimessage = (e) => {
          const d = e.data;
          if (d.length >= 2 && d[0] >= 0x80 && d[0] < 0xF0) send({ type: "midi", data: [d[0], d[1], d[2] || 0] });
        };
      }
      status.textContent = names.length ? `MIDI in: ${names.join(", ")}` : "MIDI in: no inputs connected.";
    };
    access.onstatechange = bind;
    bind();
  } catch { status.textContent = "MIDI in: not allowed (the panel and the computer keyboard still play)."; }
}

// ------------------------------------------------------------------ switching on
async function switchOn() {
  const go = $("go"), err = $("error");
  go.disabled = true;
  go.textContent = "Starting…";
  err.hidden = true;
  try {
    const ac = new AudioContext({ sampleRate: 44100, latencyHint: "interactive" });
    const [wasm] = await Promise.all([
      fetch("grimm.wasm").then((r) => { if (!r.ok) throw new Error(`grimm.wasm: ${r.status}`); return r.arrayBuffer(); }),
      ac.audioWorklet.addModule("worklet.js")]);
    node = new AudioWorkletNode(ac, "grimm", { numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2] });
    node.connect(ac.destination);
    let warned = false;
    await new Promise((res, rej) => {
      node.port.onmessage = (e) => {
        const m = e.data;
        if (m.type === "ready") res();
        else if (m.type === "error") rej(new Error(m.message));
        else if (m.type === "frame") {
          if (m.fb) drawScreen(m.fb);
          if (m.leds) drawLights(m.leds);
          if (m.settings && !saveSettings(m.settings) && !warned) {
            warned = true;
            console.warn("GR!MM: this browser does not keep the settings; they last until the page closes.");
          }
        }
      };
      const settings = loadSettings();
      node.port.postMessage({ type: "load", wasm, settings: settings && settings.buffer, master }, [wasm]);
    });
    await ac.resume();
    $("start").hidden = true;
    if (ac.sampleRate !== 44100)
      console.warn(`GR!MM: the browser plays at ${ac.sampleRate} Hz; the pitch is off by ${(1200 * Math.log2(ac.sampleRate / 44100)).toFixed(0)} cents.`);
    syncButtons();
    syncKeys();
    midiOn();
  } catch (e) {
    node = null;
    go.disabled = false;
    go.textContent = "Switch on";
    err.textContent = "Could not start: " + ((e && e.message) || e) + ". A current Chrome, Edge, Firefox or Safari should work.";
    err.hidden = false;
  }
}
$("go").addEventListener("click", switchOn);
