# OREBIT sound effects

35 one-shot cues and one sustained thrust loop from Kenney and FrogPog, all CC0-1.0. No music, AI-generated
audio, or Pixabay assets are included. Credit is optional; source attribution is
retained in `manifest.json` alongside original names, hashes, processing, and
license links. `licenses/` preserves the source license records.

Menus use quiet clicks and chips; progress and cargo use short NES-style tones;
deployment and contact use machinery and impacts.
Ship and rig touchdown use slowed, low-pass-filtered heavy punch recordings for
a bass-weighted thunk without metallic ringing; hard touchdown is stronger.
Sources are converted to mono 16-bit PCM with silence trimmed, short fades and conservative peak levels.
Both playback adapters apply 0.35 master gain and allow at most eight one-shot voices.
Thrust uses one dedicated, crossfaded engine loop with throttle-driven gain and
pitch, silenced while paused, unfocused, fuel-starved, or engines are cut.
Web thrust release fades to silence over 35ms before disconnecting, avoiding
the click caused by abruptly cutting a non-zero waveform.
Focus uses a filtered 35ms mechanical tick at 2.5% peak, with a 160ms cooldown.
Upgrades use a softened confirmation capped at 240ms instead of the long level-up
fanfare. Drilling uses a filtered rotary machinery recording, a 420ms minimum
interval and subtle downward pitch variation; cargo-credit tones remain separate.
Ore collection and deposits use short 85ms clicks, separate from mineral
contact. Level Up/progression uses a softened confirmation capped at 320ms.
The ore-collection click is boosted to 0.42 peak (3x the previous amplitude)
to remain audible over drilling; deposit volume is unchanged.
The shared low-oxygen/thermal-lock warning is pitched down about five semitones
and reduced to one-quarter of its original amplitude.
Collection tracks carried plus banked material, so rig pickups click immediately
and stowing does not double-count them. Drone ore clicks upon ship delivery.
Title-screen launch triggers the full ignition cue at animation start, suppressing
the menu activation beep. Ignition uses a slowed, filtered large-engine recording
capped at 2.6 seconds, also shared by gameplay takeoff.
Takeoff ignition is two octaves below its initial mix (0.1875x source speed),
retaining the same peak level with a longer 250ms release.
Player terrain bumps share the low rig-impact thunk. The same collision event
that refreshes the red directional indicator requests audio; its visual decay
does not. Contact stays latched until terrain clearance returns, so resting and
scraping do not refresh the flash or sound. Cutting alone is not a bonk event.
Two voices are reserved for damage, failure, hard touchdown and warnings.
Per-cue cooldowns are defined in `src/platform/GameAudioCatalog.h`.

`tools/import-sfx.py` reproduces the WAVs from the three extracted Kenney packs
(folders `ui`, `impact`, `sci`) and downloads only the listed FrogPog originals.
It requires Python, numpy and soundfile. Sources were retrieved 2026-09-12.

## Acceptance

Ship destruction from planet impact or thermal runaway plays one gritty Kenney
explosion recording at cinematic start, replacing the generic failure chirp.
It uses the reserved critical-effect voice budget and does not retrigger during
the destruction animation.

Run `node --test tools/audio-assets.test.mjs` to check manifest coverage, hashes,
PCM decoding metadata, silence/clipping, and web/native path agreement. Native
app tests cover limiter behavior and outcome/interaction event separation.

Final subjective listening on speakers/headphones and physical-controller/Deck
playthrough remain human acceptance steps. Automated checks cannot certify tone.
