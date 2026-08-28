# stoermelder AHAB — Random generator

The Random generator fills a selected area of AHAB's grid with a complete, ready-to-run ORCA arrangement. Instead of scattering random characters, it *composes*: several voices that play together, sharing one key, one scale and one rhythmic groove — leads, harmonies, call-and-response gates, drums and even MIDI modulation that follows the melody. Everything it writes is ordinary ORCA text, so you can edit, extend or throw away any part of the result afterwards.

## Generating a pattern

1. **Select** the area of the grid you want to fill (click and drag).
2. Open the **context menu** (right-click) and choose *Random generator*, then pick a density:

| Menu item | What it does |
|---|---|
| *Sparse (20%)* | A few voices, spread widely across the selection; no shared clock |
| *Medium (40%)* | Moderately filled and evenly spaced, with a shared clock bus — a good starting point |
| *Very dense (70%)* | Tightly packed; most of the selection in use |
| *Packed (100%)* | As many voices as fit, edge to edge; walls of sound |
| *Same seed again* | Repeats the last generation exactly (shown only after a first run, together with its seed number) |

Density shapes **both** halves of the result: how many voices are generated *and* how much room each one keeps from its neighbours — sparse takes scatter widely, packed takes fill edge to edge.

The same submenu holds a ***Channels*** picker: how many voices may sound at all, from 1 to 12 (default 4). Voices are chosen in a fixed order of musical priority — lead, drums, bass, harmony, chord, gate, then ambient texture — so a small budget yields a coherent core rather than a crowd. Raise it when your rack has more inputs to feed; the setting is saved with the patch and applies to every later generation, whichever density you pick.

3. Press **Run** (or the spacebar) to hear it. Make sure a MIDI output is configured — see the [main AHAB manual](./Ahab.md).

Things worth knowing:

- Only the **selected rectangle** is written. Nothing outside your selection ever changes.
- The whole selection is **replaced**, like a paste — including clearing cells you had there before.
- Very small selections may produce **nothing at all** (and no undo entry). The generator needs a bit of room to place anything meaningful.
- A generation can be reverted like any other edit with **Ctrl/Cmd + `Z`**.

## What the generator builds

For larger selections (roughly four rows and ten columns or more) you get a full arrangement. Smaller selections fall back to individual building blocks instead — see the end of this section.

Since you already read ORCA fluently, here is what the generator actually writes. Three conventions shape everything it places:

- **Writers sit above readers.** Variables live for a single tick and are only visible below (and to the right) of their writer, so any voice consuming another voice's note is placed physically below it — that is why arrangements grow downward.
- **Chains are uppercase; only the last row is banged.** All logic operators (`C`, `T`, `V`, `A`, `F`, `U`…) refresh every tick, so no verdict or value can go stale; the final `:` / `!` row is lowercase and takes its bang from a dedicated `D` row whose period divides the bar.
- **Every variable name is handed out once per take.** The clock bus publishes into `q`/`w`/`e`/`r`; each melodic voice publishes its current note into its own letter from the remaining pool, so follower chains never collide.

The grids below show real shapes as they land in the field (`.` cells that an operator pokes at runtime are left empty, as when you paste).

### The clock bus

Each bus unit is two rows — a clock writing its counter straight into a variable:

```
.1C8
qV.
```

`C` pokes `glyph_of(tick/rate%mod)` directly below itself, which is exactly the value cell of the `qV` write. Several units side by side publish divisions `q`, `w`, `e`, `r`; every modulus is a divisor of the bar, making the bus the rhythmic spine of the arrangement. At *Sparse* density the bus is omitted entirely and voices fall back to their own clocks — looser, more chaotic.

### The lead

Exactly one per take — the melody everything else arranges around. The classic ORCA arpeggio, five rows, fed by the first bus division instead of its own clock:

```
.Vq.......     q steps the track key from above
..4T1324..     track walks a 4-note scale walk
..pV......     current note published into p
.4D2..Vp..     bang + read of p parked over the note cell
...:15.f4.     ':' on the lead's own channel, MIDI 2
```

The sequence length doubles as the track modulus and is drawn from the bar's divisors, so the lead completes its walk within one bar. It plays on MIDI channel 2 and publishes `p` for anything placed below it.

### Harmony

Exactly one, three rows: read the lead's note, transpose, play.

```
...Vs...
.4D2.A2     A adds 2 letter-steps (C -> E in base36 space)
..:24.f4    note cell fed by the A output each tick
```

Because `A` adds a constant interval, a harmony moves in strict parallel motion with its lead. When there is room, the harmony also republishes its transposed note — a `vV.` row whose value cell sits exactly where the `A` pokes — so texture accents can follow the transposed line.

### Bass

Exactly one per take, planned right after the lead and drums, and only when a clock bus exists (a bass with its own free-running clock defeats the point). It reuses the lead's five-row arpeggio shape with two deliberate constraints:

- **Pinned low register** — its octave is fixed at the bottom of the arrangement's range rather than drawn with the other voices, so the foundation never moves.
- **Root and fifth only** — the track alternates between the scale's root and fifth. No wandering: a bass line that strays chromatically is not a foundation.

```
.Vr.......     r drives the track key: a slow bus division
..4TCGCG..     alternating root and fifth of the patch scale
..bV......     current note published into b
.4D2..Vb..     bang + read of b parked over the note cell
...:02.f4.     ':' on MIDI channel 1, octave pinned low
```

It plays on its own MIDI channel like any melodic voice and publishes its current note — so harmonies or gates can chain off it just as they chain off leads.

### Gate (call and response)

Exactly one, same read, but `F` compares instead of `A` adding:

```
....Vs......
.....FC.....     F pokes '*' below itself while the lead plays C
......:54Cf4     that cell is the ':'s left neighbour: the bang
```

The gate answers with its own note only while the lead holds the watched pitch. The stricter two-input variant sounds only when *two* publishers match at once — the first verdict is relayed down past the second publisher's read by `J`, both verdicts meet in `L` (which yields `.` unless both inputs are `*`), and a final `F…0` converts the double-star back into a bang:

```
...Vs.......
.....Fp.....
.Vs.........    second publisher's read; the first verdict lands beside it
...FqJ......
....L.......
.....F0.....
......:54Af4
```

### Drums

Two rows — euclidean hits on channel 10, tuned to the patch scale:

```
.sUm
...:92Gf1
```

The euclidean modulus divides the bar like every other period in the take, so the groove locks to everything else.

### Modulation (MIDI CC)

Accompanying voices may grow a two-row tail that streams the melody they follow as CC:

```
1D2..Vs     D re-bangs every 2nd tick; V-read parks over the value cell
.*!ck..     '!' sends channel c, control k, value = current pitch
```

The value cell is fed straight from the source's published note, so the CC traces the melodic contour — midicc maps glyph values 0–35 onto CC 0–127, and since note letters sit mid-alphabet the sweep stays in a musical range. The tail goes out on the same MIDI channel as the voice it decorates and targets low control numbers (CC 64–67 with the default CC range setting; adjust the CC range in the context menu if your synth maps those to something else). The bang period is deliberately pinned to 2: a slower random period could phase-lock against the melody's own period and sample the same pitch forever — a stuck CC.

### Texture

Remaining space is filled with input-less filler on the upper MIDI channels (11–16). Chords are N stacked delay+`:` pairs sharing one rate/modulus — `D` fires wherever `tick%(rate*mod)` is 0 regardless of position, so all notes land on the same tick:

```
.4D8.....
...:34Cc8
.4D8.....
...:37Cc8
```

plus extra euclidean voices and delay-triggered single hits. Any of these may grow a `K` of its own — one name cell beside the delay, poking the publisher's pitch into the hit's velocity cell:

```
.4D8.1Kp.     K feeds the velocity cell below from publisher p
...:35Cc8     velocity arrives live; the literal is only a prefill
```

so texture hits accent up and down with the melody instead of hitting at one fixed level.

### Small selections

Below roughly four rows or ten columns the generator skips arranging entirely and drops loose building blocks: clocks (`.rCm`), delay bangs (`.rDm`), euclidean rhythms (`.sUm`), random sources (`.mRx`), counters (`.sIm`), arithmetic (`A`/`M`/`F`), halt gates (`H`), offset reads (`O`) and variable round-trips — always a write paired with its own read on the same row, since a standalone read could never see a write from a previous tick. Nothing is coupled here; it is touch-up material, not a composition.

## Musical glue

A few decisions are made once per generation and applied everywhere, which is what keeps the result sounding like a piece of music rather than a jumble:

- **One key and one scale** per take (major, natural minor, pentatonic or dorian). Every note in every voice belongs to it.
- **One bar length** — 8 or 12 ticks. Every clock, delay and rhythm period in the arrangement divides it evenly, so all voices realign every bar and the whole pattern repeats as a unit.
- **Fixed channel plan**: every role owns its own MIDI channel, always the same one — bass 1, lead 2, harmony 3, chords 4, gates 6, drums 10, texture hits 11–16. Wire your rack once, then re-roll freely: the bass is always on channel 1.
- **Role-based register spread**: each role plays in its own octave band — bass low, then chords, harmonies, gates and texture, with the lead on top — so the voices stack into a range instead of piling into one pitch.

## Repeating a take you like (seeds)

Every generation starts from a *seed* — the number that produced exactly what you see. Generation is fully deterministic: the same seed with the same selection size, the same density and the same *Channels* setting reproduces the identical pattern, on any machine.

- Liked a take? Use ***Same seed again*** from the menu to get it back byte for byte.
- The last seed is **saved with your patch**, so the reproduction keeps working after reloading.
- Want a variation instead? Pick any density item again — that draws a fresh seed.

## Built-in quality control

Before a take is shown, AHAB quietly test-runs it and checks measurable facts: does anything sound at all, do at least two different pitches occur, does something happen promptly, is the grid neither nearly empty nor wall-to-wall, do all variable references resolve, and does any CC modulation actually move rather than sit stuck at one value. Candidates that fail are thrown away automatically and the best attempt of a few tries is kept.

This guarantees the output is *technically alive* — it cannot judge whether you like it. When in doubt, simply re-roll.

## Editing the result

The generated pattern is normal ORCA:

- Hover any operator for its tooltip description.
- Change notes, rates, channels, velocities — the arrangement keeps working.
- Copy or cut parts and paste them into other AHAB instances; the clipboard is shared.
- External **Clock In** and **Reset** behave exactly as with hand-made patterns.

## Limitations

Being honest about what the randomizer does *not* do:

- **It needs room.** Tiny selections yield either nothing or a few disconnected fragments. For a proper arrangement give it at least ~16×32 cells; bigger selections hold more voices.
- **It replaces, it does not evolve.** Each generation is a fresh snapshot of the selection. There is no "develop this pattern" — regenerate and hope, or edit by hand.
- **The Channels cap *is* the arrangement.** However dense the selection, no more voices sound than the *Channels* setting allows (default 4) — they are chosen lead-first, so raising it adds harmony, chords, gates and then more texture. Beyond twelve voices even the texture channels start sharing, and those voices blur into each other.
- **Overlapping notes.** Generated lines do not end the previous note when starting a new one (the MIDI note operator is polyphonic). Fast lines with long note lengths stack overlapping notes on one channel.
- **One identity per take.** Key, scale, bar length and channel plan are fixed for a single generation. A different vibe means a new generation.
- **Modulation follows pitch only.** CC tails track the melody they accompany; there is no modulation from drums or from LFO-style free movement, and no pitch-bend is generated.
- **Velocity accents, not swells.** Live velocity rides midi's non-linear mapping (glyph index × 8 − 1, clamped at max from index 16), so pitch-letter sources land in the loud upper range — you get accent differences between lines, not smooth crescendi.
- **No song structure.** The result is one repeating bar — no intros, fills or arrangement dynamics over time.
- **No taste guarantee.** The quality gate filters out broken and silent output, not boring output. Curating takes is your job; seeds make that cheap.
