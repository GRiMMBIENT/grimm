# SPDX-License-Identifier: GPL-3.0-only
# Copyright (C) 2026 GR!MM
"""The stress test's script (tests/stress.sh): host.c's script lines, and the times tests/stress_check.c checks that
everything let go has stopped. Seven rounds: every source on every track (round r: track t plays source (r + t) mod 7),
each track's chord mode, CHORD TYPE, VOICING and LATCH at random, SCALE off or set; then a burst of playing: white and
black keys fast, held and let go, chords of keys, MIDI notes in (doubled note-ons too), the lowest and the highest
octave, every page, knobs, PRESETS, the hold combos (SAVE + keys: slots; EDIT + OCT: undo / redo; SEL held; ARP held;
FX + F3; SEQ + SELECT; HOME held), SAVE's pages' actions, PLAY and REC (live and step recording; the arp's notes
recorded); each track's ARP on or off, its MODE, RATE, OCTAVES, GATE at random; each track's LFO (RATE, DEPTH,
DESTINATION, SHAPE) at random; GLO held + keys: mutes and solos; SONG's chain and MODE at random, song record (REC on
the SONG page, PLAY, slots switched to, STOP); FX held + the tricks' keys (G3 .. F4), held and let go at random, over
each other, FX let go first now and then; then everything let go
(keys, MIDI note-offs, STOP, REC off, every track's LATCH OFF) and checked. At the end, a long wait: every release over.
First, a project saved as a player's might be (every track LATCH ON, chord mode on, a SCALE), autosaved, come back from as at
power-on, and one key pressed; slot A saved; the project and the slots put in flash as version 5 wrote them and come
back from (the upgrade); autosaved; a save of the slots cut half way (a power cut). In the rounds also: SEQ held +
the tools' keys (CLEAR, RANDOM, SHIFT, COPY, PASTE, DOUBLE, MUTATE), SEL held on SEQ's pages + the knobs (step
locks), SEL + OCT- (the locks cleared); steps recorded in chord mode (chord steps). Then SYSTEM (HOME held): COLOUR,
MIDI CHANNEL 2-5, TUNE +7, SCREEN OFF 1 MIN; SYSTEM 2: SEQ MIDI OUT on, MIDI LEARN (ENV's ATTACK, CC 21); the settings
saved; a minute untouched (the screen dark), a key (it only wakes it). In the rounds also: CCs (linked or not), MIDI
LEARN on any page's knob (OCT- cancelling), MIDI CHANNEL changed with notes on, SEQ MIDI OUT off and on, CLEAR ALL.
Then a user sound saved (SAVE > SOUNDS) and PRESETS turned through the 40 factory sounds into the user's, checked on
the way and at the end; the editor's mailbox (x_editor: reads, writes, refusals); from the rounds on, garbage to it
every 47 ms (y_garbage). HOME's CUTOFF, RESONANCE, FX on every track in each round (T1's set before
the power-on, checked back; the upgrade's old saves: open, 100 %); the LFO's TRK CUTOFF among its destinations.
FM: on each FM track in each round FM ALGO and FM OP's knobs at random (ALGO, FEEDBACK, OP, FINE; RATIO, LEVEL,
ATTACK, DECAY), PATCH now and then (T1's patch edited before the power-on, checked back); the sounds as saved before
step 24 come back from (v_sounds); an FM patch through a sound and a slot after the rounds' first (m_fm).

    python3 tests/stress_gen.py SEED SCRIPT CHECKS"""
import random
import sys

OCTD, OCTU, FX, SEL, ENV, LFO, EDIT, GLO, HOME, SAVE, ARP, SEQ, PLAY, REC = range(14)
SELECT, ALGO, K1, K2, K3, K4, PRESETS = range(7)
PAGE_BTNS = [EDIT, ENV, LFO, SEL, GLO, HOME, SAVE, ARP, SEQ, FX]
WHITE = [0, 2, 4, 6, 7, 9, 11, 12, 14, 16, 18, 19, 21, 23, 24, 26]
BLACK = [1, 3, 5, 8, 10, 13, 15, 17, 20, 22, 25]
WK = [0, 2, 4, 6, 7, 9, 11, 12]                  # SAVE held: white keys 1..4 recall A..D, 5..8 save

seed = int(sys.argv[1]) if len(sys.argv) > 1 else 1
rnd = random.Random(seed)
ev, checks = [], []
t = 1700                                        # (the boot screen still: the keys act as ever)
keys = 0                                        # the keys held down
midi_on = set()                                 # MIDI notes on (sent, not yet off): (channel, note)
playing = rec = False


def at(dt, s):
    global t
    t += dt
    ev.append(f"{t} {s}")


def tap(b, dt=25):
    at(dt, f"btn {b}")


def turn(e, n, dt=15):
    if n:
        at(dt, f"enc {e} {n}")


def set_keys(m, dt):
    global keys
    keys = m
    at(dt, f"keys {m}")


def page(b, n=1):                               # a known start (HOME's), then button b tapped n times
    tap(HOME)
    for _ in range(n):
        tap(b)


def source_page():                              # EDIT's SOURCE (an FM track has FM ALGO, FM OP between: the
    tap(HOME)                                   # checks put the page there, as EDIT tapped till it is)
    checks.append(f"{t + 5} g_source")
    at(25, f"keys {keys}")


def track_page():                               # HOME (its knobs the track's VOLUME, CUTOFF, RESONANCE, FX), from
    tap(EDIT); tap(HOME)                        # anywhere (EDIT, HOME)


def track(i):
    turn(ALGO, -5)
    turn(ALGO, i)


def setup(r):
    page(SEL)                                   # SCALE: ROOT, SCALE (OFF half of the time)
    turn(K1, -20); turn(K1, rnd.randrange(12))
    turn(K2, -20); turn(K2, 0 if rnd.random() < 0.4 else rnd.randrange(1, 14))
    for i in range(4):
        track(i)
        source_page()                           # SOURCE: the round's source, LATCH
        turn(K1, -10); turn(K1, (r + i) % 7)
        turn(K2, rnd.choice([-1, 1]))
        if (r + i) % 7 == 1:                    # FM: FM ALGO, FM OP (EDIT from SOURCE: SOUND, FM ALGO, FM OP)
            tap(EDIT)
            if rnd.random() < 0.3:
                turn(K1, rnd.choice([-1, 1, 2]))
            tap(EDIT)
            for k in (K1, K2, K3, K4):
                turn(k, rnd.randint(-12, 12))
            tap(EDIT)
            for k in (K1, K2, K3, K4):
                turn(k, rnd.randint(-40, 40))
        page(SEL, 2)                            # CHORD: CHORD TYPE, VOICING; chord mode on or off (SEL held)
        turn(K1, -20); turn(K1, rnd.randrange(11))
        turn(K2, -20); turn(K2, rnd.randrange(5))
        if rnd.random() < 0.6:
            at(25, f"down {SEL}"); at(700, f"up {SEL}")
        page(ARP)                               # ARP: MODE, RATE, OCTAVES, GATE; on or off (ARP held)
        turn(K1, -9); turn(K1, rnd.randrange(6))
        turn(K2, -12); turn(K2, rnd.randrange(9))
        turn(K3, -5); turn(K3, rnd.randrange(4))
        turn(K4, rnd.choice([-100, -20, 20, 100]))
        if rnd.random() < 0.6:
            at(25, f"down {ARP}"); at(700, f"up {ARP}")
        page(LFO)                               # LFO: RATE, DEPTH (0 a third of the time), DESTINATION, SHAPE
        turn(K1, -127); turn(K1, rnd.choice([1, 40, 80, 110, 127]))
        turn(K2, -100); turn(K2, 0 if rnd.random() < 0.3 else rnd.choice([5, 30, 70, 100]))
        turn(K3, -10); turn(K3, rnd.randrange(10))
        turn(K4, -10); turn(K4, rnd.randrange(7))
        track_page()                            # HOME: CUTOFF (open a third of the time), RESONANCE, FX
        turn(K2, 127 if rnd.random() < 0.33 else -rnd.randrange(128))
        turn(K3, -127); turn(K3, rnd.choice([0, 30, 90, 127]))
        turn(K4, -200); turn(K4, rnd.choice([0, 50, 100, 150, 200]))
    track(rnd.randrange(4))


def combo():
    b = rnd.choice([SAVE, EDIT, SEL, ARP, GLO, GLO, FX, SEQ, HOME])
    at(10, f"down {b}")
    if b == GLO:                                # mutes, solos (white keys 1..4, 5..8)
        for _ in range(rnd.randint(1, 3)):
            k = rnd.choice(WK)
            set_keys(keys | 1 << k, rnd.randint(20, 120))
            set_keys(keys & ~(1 << k), rnd.randint(20, 120))
    elif b == SAVE:                             # slots: recall, save (again: overwrite)
        for _ in range(rnd.randint(1, 3)):
            k = rnd.choice(WK)
            set_keys(keys | 1 << k, rnd.randint(20, 120))
            set_keys(keys & ~(1 << k), rnd.randint(20, 120))
    elif b == EDIT:                             # undo, redo
        for _ in range(rnd.randint(1, 3)):
            tap(rnd.choice([OCTD, OCTU]), rnd.randint(20, 150))
    elif b == FX:                               # FREEZE now and then; the tricks, held over each other; a black key
        if rnd.random() < 0.3:
            set_keys(keys | 1, 30); set_keys(keys & ~1, rnd.randint(30, 200))
        tk = rnd.sample(WK[1:], rnd.randint(1, 4))
        for k in tk:
            set_keys(keys | 1 << k, rnd.randint(10, 400))
        if rnd.random() < 0.5:
            k = rnd.choice(BLACK)
            set_keys(keys | 1 << k, 30); set_keys(keys & ~(1 << k), rnd.randint(30, 200))
        if rnd.random() < 0.3:                  # FX let go first, the tricks' keys after
            at(rnd.randint(50, 600), f"up {b}")
            for k in tk:
                set_keys(keys & ~(1 << k), rnd.randint(10, 1500))
            return
        for k in tk:
            set_keys(keys & ~(1 << k), rnd.randint(10, 1500))
    elif b == SEQ:                              # the track's STEPS; the tools (white keys 1..8)
        if rnd.random() < 0.4:
            turn(SELECT, rnd.randint(-8, 8), 50)
        for _ in range(rnd.randint(1, 3)):
            k = rnd.choice(WK)
            set_keys(keys | 1 << k, rnd.randint(20, 120))
            set_keys(keys & ~(1 << k), rnd.randint(20, 120))
    else:                                       # SEL: chord mode; ARP: the arp; HOME: SYSTEM (held 0.7 s); keys
                                                # meanwhile
        for _ in range(rnd.randint(0, 3)):
            k = rnd.randrange(27)
            set_keys(keys | 1 << k, rnd.randint(20, 150))
            set_keys(keys & ~(1 << k), rnd.randint(20, 150))
    at(rnd.choice([30, 300, 750]), f"up {b}")


def burst(ms):
    global playing, rec
    end = t + ms
    while t < end:
        x = rnd.random()
        if x < 0.30:                            # a key down or up (white, black)
            k = rnd.choice(BLACK if rnd.random() < 0.5 else WHITE)
            set_keys(keys ^ 1 << k, rnd.choice([5, 15, 40, 120, 400]))
        elif x < 0.36:                          # a chord of keys, held, let go
            ks = rnd.sample(range(27), rnd.randint(2, 4))
            m = sum(1 << k for k in ks)
            set_keys(keys | m, 10); set_keys(keys & ~m, rnd.randint(50, 600))
        elif x < 0.40:                          # a fast run
            for _ in range(rnd.randint(4, 12)):
                k = rnd.randrange(27)
                set_keys(keys | 1 << k, 25); set_keys(keys & ~(1 << k), 25)
        elif x < 0.48:                          # MIDI in: on (doubled sometimes), off, on its channel
            if midi_on and rnd.random() < 0.5:
                c, n = rnd.choice(sorted(midi_on))
                at(rnd.randint(5, 80), f"midi {0x80 | c} {n} 0"); midi_on.discard((c, n))
            else:
                c = rnd.choice([0, 1, 2, 3, 4, rnd.randrange(16)])
                n = rnd.choice([0, 1, 12, 36, 60, 61, 63, 66, 100, 120, 126, 127, rnd.randrange(128)])
                at(rnd.randint(5, 80), f"midi {0x90 | c} {n} 100"); midi_on.add((c, n))
        elif x < 0.52:                          # the octave: the lowest, the highest, the middle
            page(HOME)
            d = rnd.choice([OCTD, OCTU])
            for _ in range(5):
                tap(d, 15)
            if rnd.random() < 0.4:
                for _ in range(2):
                    tap(OCTU if d == OCTD else OCTD, 15)
        elif x < 0.62:                          # a page
            tap(rnd.choice(PAGE_BTNS), rnd.randint(20, 200))
        elif x < 0.72:                          # a knob on it, PRESETS
            turn(rnd.choice([K1, K2, K3, K4, K1, K2, K3, K4, PRESETS]), rnd.choice([-9, -3, -1, 1, 2, 5, 9]),
                 rnd.randint(10, 100))
        elif x < 0.80:
            combo()
        elif x < 0.84:                          # PLAY / STOP
            tap(PLAY, rnd.randint(20, 300))
        elif x < 0.87:                          # REC (step record stopped, live record playing; SONG: song record)
            tap(REC, rnd.randint(20, 300))
        elif x < 0.90:                          # SAVE's pages: SOUNDS' and PROJECTS' actions, then back
            tap(SAVE)
            turn(K1, rnd.randint(-5, 5)); turn(K4, rnd.choice([-3, 1, 2])); tap(OCTU)
            tap(SAVE)
            turn(K1, rnd.randint(-3, 3)); turn(K4, rnd.choice([-3, 1, 2, 3])); tap(OCTU)
            tap(OCTD)
        elif x < 0.905:                         # SEL held on SEQ's pages: the knobs lock the step; OCT-: cleared
            page(SEQ, rnd.choice([1, 2]))
            turn(SELECT, rnd.randint(-6, 6))
            at(20, f"down {SEL}")
            for _ in range(rnd.randint(0, 3)):
                turn(rnd.choice([K1, K2, K3, K4]), rnd.choice([-9, -2, 1, 3, 9]), rnd.randint(20, 200))
            if rnd.random() < 0.2:
                tap(OCTD, 50)
            at(rnd.choice([100, 400, 800]), f"up {SEL}")
        elif x < 0.915:                         # SONG: a step of the chain, MODE; song record now and then
            page(GLO)
            turn(K1, rnd.randint(-15, 15)); turn(K2, rnd.choice([-4, -2, -1, 1, 4])); turn(K3, rnd.randint(-3, 3))
            turn(K4, rnd.choice([-2, -1, 1, 2]))
            if rnd.random() < 0.4:
                tap(REC, 30); tap(PLAY, rnd.randint(50, 900))
                for _ in range(rnd.randint(1, 3)):
                    at(rnd.randint(200, 1500), f"down {SAVE}")
                    k = rnd.choice(WK[:4])
                    set_keys(keys | 1 << k, 40); set_keys(keys & ~(1 << k), 40)
                    at(40, f"up {SAVE}")
                tap(rnd.choice([PLAY, REC]), rnd.randint(300, 2500))
        elif x < 0.93:                          # another track
            turn(ALGO, rnd.choice([-1, 1]), rnd.randint(20, 100))
        elif x < 0.945:                         # CCs (21: WOW's link); MIDI LEARN; MIDI CHANNEL, SEQ MIDI OUT, CLEAR ALL
            y = rnd.random()
            if y < 0.6:
                for _ in range(rnd.randint(1, 6)):
                    at(rnd.randint(5, 60), f"midi {0xB0 | rnd.randrange(16)} {rnd.choice([21, 21, 7, 74, rnd.randrange(128)])} "
                                           f"{rnd.randrange(128)}")
            elif y < 0.85:                      # LEARN, a page's knob, a CC (OCT- cancelling now and then)
                page(HOME); at(20, f"down {HOME}"); at(700, f"up {HOME}"); tap(HOME)
                turn(K1, -3); turn(K1, 1)
                tap(rnd.choice(PAGE_BTNS))
                turn(rnd.choice([K1, K2, K3, K4]), rnd.choice([-2, 1, 3]))
                if rnd.random() < 0.15:
                    tap(OCTD)
                at(30, f"midi 0xB0 {rnd.randrange(128)} {rnd.randrange(128)}")
            else:                               # MIDI CHANNEL (notes on); SYSTEM 2: SEQ MIDI OUT off, on; CLEAR ALL
                page(HOME); at(20, f"down {HOME}"); at(700, f"up {HOME}")
                if rnd.random() < 0.5:
                    turn(K2, rnd.choice([-14, -1, 1, 3, 14]))
                else:
                    tap(HOME)
                    if rnd.random() < 0.6:
                        turn(K2, -1); turn(K2, 1, rnd.randint(20, 900))
                    else:
                        turn(K1, -3); turn(K1, 2); tap(OCTU)
        else:
            at(rnd.randint(100, 900), f"keys {keys}")


def let_go(silent=False):
    global playing, rec, keys
    set_keys(0, 20)
    for c, n in sorted(midi_on):
        at(5, f"midi {0x80 | c} {n} 0")
    midi_on.clear()
    page(HOME)                                  # PLAY, REC, song record off, whatever they are ("stop")
    checks.append(f"{t} p")
    at(120, "keys 0")
    for i in range(4):                          # every track's LATCH OFF: its latched notes released
        track(i)
        source_page()
        turn(K2, -1)
    at(25000 if silent else 150, f"keys 0")
    checks.append(f"{t} {'silent' if silent else 'released'}")


page(SEL)                                       # the project of a power-on: SCALE set, every track LATCH ON and chord
turn(K2, 1 + rnd.randrange(13))                 # mode on; autosaved (stopped, untouched 2.5 s, silent); back from it
for i in range(4):
    track(i)
    source_page()
    turn(K2, 1)
    page(SEL, 2)
    turn(K2, rnd.randrange(5))
    at(25, f"down {SEL}"); at(700, f"up {SEL}")
track(0)                                        # T1's TRACK: a filter, FX 150 % (back at power-on)
track_page()
turn(K2, -67); turn(K3, 50); turn(K4, 50)
source_page()                                   # T1 FM, its patch edited (back at power-on), then VA again
turn(K1, 1)
tap(EDIT); tap(EDIT)
turn(K1, 7); turn(K2, 3); turn(K3, 2); turn(K4, 30)
tap(EDIT)
turn(K1, 3); turn(K2, -20); turn(K3, 15); turn(K4, 25)
tap(EDIT)
turn(K1, -1)
at(4000, "keys 0")
checks.append(f"{t} boot")
page(HOME)
k = rnd.choice(WHITE + BLACK)
set_keys(1 << k, 50); set_keys(0, 400)          # one key: latched, its chord
at(3000, "keys 0")
page(HOME)                                      # slot A saved (SAVE held + C4), B too
at(20, f"down {SAVE}"); set_keys(1 << 7, 40); set_keys(0, 40); set_keys(1 << 9, 40); set_keys(0, 40); at(40, f"up {SAVE}")
at(200, "keys 0")
checks.append(f"{t} upgrade")                   # version 5's project and slots, come back from
for i in range(4):                              # (every track LATCH OFF: the autosave waits for silence)
    track(i)
    source_page()
    turn(K2, -1)
at(25000, "keys 0")                             # autosaved (at most every 20 s): the project, the slots as the stream
checks.append(f"{t} cut")                       # a save of the slots cut half way
at(500, "keys 0")
checks.append(f"{t} v_sounds")                  # the sounds as saved before step 24
at(200, "keys 0")
page(HOME)                                      # SYSTEM (HOME held): TEAL, MIDI 2-5, TUNE +7, SCREEN OFF 1 MIN
at(20, f"down {HOME}"); at(700, f"up {HOME}")
turn(K1, 2); turn(K2, 1); turn(K3, 7); turn(K4, 1)
tap(HOME)                                       # SYSTEM 2: SEQ MIDI OUT ON, LEARN; ATTACK turned; CC 21 linked,
turn(K2, 1); turn(K1, 1)                        # then at 127 (ENV's ATTACK: the selected track's)
tap(ENV)                                        # ENV: the track's ATTACK
turn(K1, -3)
at(30, "midi 0xB0 21 5")
at(30, "midi 0xB0 21 127")
at(30, "keys 0")
checks.append(f"{t} learned")
at(3000, "keys 0")                              # the settings saved (2 s after, silent)
checks.append(f"{t} e_settings")
checks.append(f"{t + 10} h_links")              # MIDI LEARN's links of steps 20..24 (HOME's T1..T4 LEVEL, TRACK)
at(30, "keys 0")
at(61000, "keys 0")                             # a minute untouched: dark; a key: woken, nothing played
checks.append(f"{t} dark")
set_keys(1 << 4, 50)
checks.append(f"{t} woken")
set_keys(0, 50)
tap(SAVE)                                       # a user sound: SOUNDS, slot 1, DRONE, SAVE (OCT+), back (OCT-)
turn(K4, 1); tap(OCTU, 40); tap(OCTD, 300)
page(HOME)                                      # PRESETS through the factory sounds, into the user's
for i in range(45):
    turn(PRESETS, 1, 30)
    if i == 2:
        checks.append(f"{t + 5} factory")
at(30, "keys 0")
checks.append(f"{t} factory")
at(50, "keys 0")
checks.append(f"{t} x_editor")                  # the editor's mailbox: GET, PUT, refusals
at(100, "keys 0")
checks.append(f"{t} y_garbage")                 # from here on: garbage to it, every 47 ms
for r in range(7):
    setup(r)
    burst(7000)
    let_go()
    if r == 0:
        checks.append(f"{t} m_fm")              # an FM patch through a sound, a slot
        at(100, "keys 0")
let_go(silent=True)
at(50, "end")
with open(sys.argv[2], "w") as f:
    f.write("\n".join(ev) + "\n")
with open(sys.argv[3], "w") as f:
    f.write("\n".join(checks) + "\n")
