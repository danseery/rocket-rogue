# OREBIT sound effects

34 one-shot cues and one sustained thrust loop from Kenney and FrogPog, all CC0-1.0. No music, AI-generated
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
Focus uses a filtered 35ms mechanical tick at 2.5% peak, with a 160ms cooldown.
Upgrades use a softened confirmation capped at 240ms instead of the long level-up
fanfare. Ore contact uses a slowed, filtered mining recording, a 420ms minimum
interval and subtle downward pitch variation; cargo-credit tones remain separate.
Ore collection and deposits use quiet 65–85ms clicks, separate from mineral
contact. Level Up/progression uses a softened confirmation capped at 320ms.
Title-screen launch triggers the full ignition cue at animation start, suppressing
the menu activation beep. Ignition uses a slowed, filtered large-engine recording
capped at 1.3 seconds, also shared by gameplay takeoff.
Player terrain bumps share the low rig-impact thunk. The same collision event
that refreshes the red directional indicator requests audio; its visual decay
does not. The shared 300ms impact cooldown limits sustained scraping.
Two voices are reserved for damage, failure, hard touchdown and warnings.
Per-cue cooldowns are defined in `src/platform/GameAudioCatalog.h`.

`tools/import-sfx.py` reproduces the WAVs from the three extracted Kenney packs
(folders `ui`, `impact`, `sci`) and downloads only the listed FrogPog originals.
It requires Python, numpy and soundfile. Sources were retrieved 2026-09-12.

## Acceptance

Run `node --test tools/audio-assets.test.mjs` to check manifest coverage, hashes,
PCM decoding metadata, silence/clipping, and web/native path agreement. Native
app tests cover limiter behavior and outcome/interaction event separation.

Final subjective listening on speakers/headphones and physical-controller/Deck
playthrough remain human acceptance steps. Automated checks cannot certify tone.
