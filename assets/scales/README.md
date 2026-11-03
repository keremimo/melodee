# SCL catalogue

The 16 original scale IDs remain unchanged. New scales are appended in the order below;
IDs are stored in projects, presets and editor files, so never reorder existing entries.

A scale says which notes are used; a tuning says what pitch they have. Sixteen entries that were really
tunings or a scale and a tuning together are **retired**: the ten twelve-note tunings (IDs 42, 43, 49–56)
and the six quarter-tone maqams (IDs 64–69). Their stored IDs, pitch tables and degree addresses stay, so a
project holding one plays the same notes and pitches, but SCALES lists one only while it is selected, and the
SCL list shows no Tuning row for it. New work uses the Tuning row, on a 12-tone scale.

`tunings.json` lists the tunings; IDs are stored in track parameter 47, shared across tracks like SCL.

The Scala-derived numerical tunings and their original descriptions are from the
[Huygens–Fokker Scala archive, version 94, March 2026](https://www.huygens-fokker.org/scala/downloads.html).
The archive was collected primarily by John H. Chalmers and Manuel Op de Coul; the individual
files identify their authors and historical sources. Equal divisions are generated directly
as 1200 × degree / division. The six maqam scales use idealised quarter tones and an octave repeat (including the simplified
SABA keyboard model), informed by
[Maqam World](https://www.maqamworld.com/en/maqam.php); actual regional intonation and melodic
practice vary. These selections describe pitch collections, rather than full maqam performance rules.
The gamelan selections are specific measured or just-intonation examples, not universal tunings.

A tuning keeps the keys' notes and moves their pitch, in every QNT mode including OFF. Temperaments stay fixed
on C, so each key keeps its colour; just and maqam tunings are built on ROOT + TRN. A maqam tuning lowers keys
by 50 cents, written on the natural above each half-flat so a maqam plays as notated: Rast lowers the third and
seventh keys above the root (E and B on C). Picking a maqam scale also sets its tuning. Leaving it puts Equal
back, unless the tuning was changed meanwhile. Each maqam scale on its tuning plays exactly the pitches of the
retired entry it took over from; `tools/gen_scales.py` checks this. A microtonal selection and drums pass a tuning by.

| Tuning | Built on | Definition |
| --- | --- | --- |
| Equal | | No offset |
| Just chromatic | ROOT + TRN | ji_12.scl |
| 7-limit just | ROOT + TRN | ji_12a.scl |
| Pythagorean | C | pyth_12.scl |
| Meantone 1/4 | C | meanquar.scl |
| Meantone 1/3 | C | meanthird.scl |
| Meantone 1/6 | C | meansixth.scl |
| Werckmeister 3 | C | werck3.scl |
| Kirnberger 3 | C | kirnberger3.scl |
| Vallotti | C | vallotti.scl |
| Young 2 | C | young2.scl |
| Rast | ROOT + TRN | Keys 4, 11 above the root −50 cents |
| Bayati, saba | ROOT + TRN | Key 2 −50 cents |
| Husayni | ROOT + TRN | Keys 2, 9 −50 cents |
| Sikah | ROOT + TRN | Keys 2, 4, 6, 9, 11 −50 cents |

QNT OFF retains the normal 12-tone chromatic keyboard and bypasses a microtonal selection, a retired entry
included, as it always has. With a microtonal selection and QNT enabled,
MIDI note address 60 is the root at C4 + ROOT + TRN. ALL advances one degree per key, WHITE advances
one degree per white key, MPC advances one degree per pad, and SNAP finds the scale tone nearest a
conventional input pitch. ROOT and TRN remain semitone offsets; octave buttons and ARP OCT advance
one scale period. Bohlen–Pierce repeats at 3:1, and the measured SLENDR period is 1208 cents.
Microtonal notes read by the semitone they sound nearest and the cents off it (E4-14); past the MIDI range,
by the pitch they are held at. DIA3/DIA7 stack every other degree; fixed chord shapes choose the nearest available
degrees to just ratios (MAJ 4:5:6, MIN 10:12:15, DOM7 4:5:6:7, MAJ7 8:10:12:15, MIN7 10:12:15:18, SUS4 6:8:9,
POW 2:3:4), on every microtonal selection and retired entry. This changes the notes a fixed chord plays on an old
project; saved notes are unaffected, since chords expand into notes at the press. Voicings move notes by whole
scale periods.

The existing 0–127 note addresses remain the storage and note identity limit. Microtonal degree
addresses outside that range are silent. ALL and MPC also leave out pitches outside the conventional
MIDI pitch range; WHITE clamps acoustic pitch at the endpoints, like the legacy white layout.
Dense tunings therefore have a narrower accessible register. MIDI OUT sends the semitone nearest
each pitch, ROOT and TRN included, without tuning messages or per-note pitch bends. It is fixed at
the press, so a release reaches the same note whatever changed meanwhile. Degrees that share a
semitone strike it again, and it ends with the last of them. MIDI OUT keeps 32 held presses across all
tracks, a sustain pedal's included; past that a note-on sends nothing until one ends, while an instrument track
still sounds it. A tuned 12-tone scale sends its plain notes. The internal synthesizers use
fractional pitch, including FM6 ratio operators; FM6 fixed-frequency operators retain their fixed
pitch. A4 reference tuning, pitch bend and semitone transpose still apply.

MPC Sample banks E, F, G and H play tracks 1, 2, 3 and 4 respectively, on any incoming MIDI channel.
Each melodic bank starts the same scale layout, with pad 02 on DEG. In MPC mode the octave buttons shift
only the selected track's bank (−3..+3; both buttons reset it), reflected in the header and button lights.
The bank octaves survive sound/track changes and are saved with projects and templates; older files
start all banks at zero. Other QNT modes retain the independent shared keyboard octave.
The latest valid MPC pad note silently selects its track, so the controls and octave indicator follow
the bank being played. Releases and ignored pads keep the selection; normal MIDI retains its routing notices.
Banks A–D and absent tracks are silent. This uses the default program pad map (E: 100–115,
F: 116–127 then 0–3, G: 4–19, H: 20–35), overriding ROUT and DRUM for note destinations.
DRUM tracks use a fixed finger-drumming map instead: pads 01–04 Kick, Snare, Clap, Rim;
05–08 Closed hat, Open hat, Cowbell (909: Ride), Cymbal (909: Crash); 09–11 low/mid/high Toms.
On 808/SID, 12 is Maraca and 13–16 low/mid/high Congas and Clave. On 909, 12–16 repeat Ride,
Kick, Snare, Closed hat and Open hat; each pad retriggers even while its duplicate is held. Pad 01 is bottom left. Drum pad identities ignore bank octave,
scale, ROOT, TRN and DEG; native drum keys retain their chromatic mapping and octave. Normal MIDI
drum notes outside MPC mode and existing recorded patterns retain their GM mapping.

Scale, tuning, QNT and MPC DEG are shared across tracks; ROOT and TRN remain per track. A voice retains the
pitch it started with when scale settings change while held or releasing. Playback interprets saved
degrees using the currently selected scale and root. The original IDs, project formats and 12-tone
behavior are preserved. Older firmware does not understand the added scale IDs: it reads 70–75 as its last
entry and ignores a project's tuning.

The SCALES browser groups selections into Western, equal divisions, just intonation,
harmonic, regional, non-octave and maqam families. Historical holds only retired entries, so it
appears only while one is selected. ALL shows the complete list; FAV shows saved selections. Device favorites
use tagged bytes 16–31 in retired engine 14's PER5 favorites row, leaving its former preset bits 0–127 and all
file layouts unchanged. A maqam scale's star is stored at its retired entry's ID, so old stars carry over; a star
on a retired twelve-note tuning no longer shows, as tunings have no favorites.
Web favorites are stored separately in the browser by stable short scale name.

`catalog.json` and `tunings.json` are the source of truth. To update checked-in firmware tables and the web editor's
mock catalogue, run `python3 tools/gen_scales.py`; `--check` verifies that both are current.
Builds and host tests perform that check without requiring a network connection.

| ID | SCL | Family | Definition |
| --- | --- | --- | --- |
| 0 | CHR | Original | 12-tone scale |
| 1 | MAJ | Original | 12-tone scale |
| 2 | MIN | Original | 12-tone scale |
| 3 | DOR | Original | 12-tone scale |
| 4 | MIX | Original | 12-tone scale |
| 5 | PEN | Original | 12-tone scale |
| 6 | MPEN | Original | 12-tone scale |
| 7 | HARM | Original | 12-tone scale |
| 8 | PHRY | Original | 12-tone scale |
| 9 | LYD | Original | 12-tone scale |
| 10 | LOC | Original | 12-tone scale |
| 11 | MEL | Original | 12-tone scale |
| 12 | BLUES | Original | 12-tone scale |
| 13 | WHOLE | Original | 12-tone scale |
| 14 | DIMHW | Original | 12-tone scale |
| 15 | DIMWH | Original | 12-tone scale |
| 16 | 5EDO | Equal divisions | 5 divisions |
| 17 | 7EDO | Equal divisions | 7 divisions |
| 18 | 9EDO | Equal divisions | 9 divisions |
| 19 | 10EDO | Equal divisions | 10 divisions |
| 20 | 13EDO | Equal divisions | 13 divisions |
| 21 | 14EDO | Equal divisions | 14 divisions |
| 22 | 15EDO | Equal divisions | 15 divisions |
| 23 | 16EDO | Equal divisions | 16 divisions |
| 24 | 17EDO | Equal divisions | 17 divisions |
| 25 | 18EDO | Equal divisions | 18 divisions |
| 26 | 19EDO | Equal divisions | 19 divisions |
| 27 | 20EDO | Equal divisions | 20 divisions |
| 28 | 21EDO | Equal divisions | 21 divisions |
| 29 | 22EDO | Equal divisions | 22 divisions |
| 30 | 23EDO | Equal divisions | 23 divisions |
| 31 | 24EDO | Equal divisions | 24 divisions |
| 32 | 26EDO | Equal divisions | 26 divisions |
| 33 | 27EDO | Equal divisions | 27 divisions |
| 34 | 29EDO | Equal divisions | 29 divisions |
| 35 | 31EDO | Equal divisions | 31 divisions |
| 36 | 34EDO | Equal divisions | 34 divisions |
| 37 | 36EDO | Equal divisions | 36 divisions |
| 38 | 41EDO | Equal divisions | 41 divisions |
| 39 | 53EDO | Equal divisions | 53 divisions |
| 40 | JI-MAJ | Just intonation | ptolemy.scl |
| 41 | JI-MIN | Just intonation | ptolemy_diat.scl |
| 42 | JI-12 | Just intonation | ji_12.scl (retired: Just chromatic) |
| 43 | JI-7L | Just intonation | ji_12a.scl (retired: 7-limit just) |
| 44 | JI-7 | Just intonation | ji_7.scl |
| 45 | HARM8 | Harmonic | harm8.scl |
| 46 | HARM16 | Harmonic | harm16.scl |
| 47 | PARTCH | Just intonation | partch_43.scl |
| 48 | HARRIS | Just intonation | harrison_8.scl |
| 49 | PYTH | Historical | pyth_12.scl (retired: Pythagorean) |
| 50 | MT-1/4 | Historical | meanquar.scl (retired: Meantone 1/4) |
| 51 | MT-1/3 | Historical | meanthird.scl (retired: Meantone 1/3) |
| 52 | MT-1/6 | Historical | meansixth.scl (retired: Meantone 1/6) |
| 53 | WERCK3 | Historical | werck3.scl (retired: Werckmeister 3) |
| 54 | KIRN3 | Historical | kirnberger3.scl (retired: Kirnberger 3) |
| 55 | VALLOT | Historical | vallotti.scl (retired: Vallotti) |
| 56 | YOUNG2 | Historical | young2.scl (retired: Young 2) |
| 57 | RAST-J | Regional | rast_11-limit.scl |
| 58 | SABA-J | Regional | saba_sup.scl |
| 59 | SHRUTI | Regional | indian.scl |
| 60 | SLENDR | Regional | slendro_av.scl |
| 61 | PELOG | Regional | alves_pelog.scl |
| 62 | BP-ET | Non-octave | bohlen-p_et.scl |
| 63 | BP-JI | Non-octave | bohlen-p.scl |
| 64 | RAST-D | Maqam models | Quarter-tone model by degree (retired: ID 70) |
| 65 | BAYAT-D | Maqam models | Quarter-tone model by degree (retired: ID 71) |
| 66 | HIJAZ-D | Maqam models | Quarter-tone model by degree (retired: ID 72) |
| 67 | SABA-D | Maqam models | Quarter-tone model by degree (retired: ID 73) |
| 68 | HUSAY-D | Maqam models | Quarter-tone model by degree (retired: ID 74) |
| 69 | SIKAH-D | Maqam models | Quarter-tone model by degree (retired: ID 75) |
| 70 | RAST | Maqam models | 12-tone scale, tuning Rast |
| 71 | BAYATI | Maqam models | 12-tone scale, tuning Bayati, saba |
| 72 | HIJAZ | Maqam models | 12-tone scale, tuning Equal |
| 73 | SABA | Maqam models | 12-tone scale, tuning Bayati, saba |
| 74 | HUSAYN | Maqam models | 12-tone scale, tuning Husayni |
| 75 | SIKAH | Maqam models | 12-tone scale, tuning Sikah |
