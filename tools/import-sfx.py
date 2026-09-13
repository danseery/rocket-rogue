"""Import the curated CC0 recordings, converting to peak-limited PCM WAV.

Usage: python tools/import-sfx.py <directory with extracted Kenney ui/impact/sci packs>
Requires soundfile and numpy. Downloads only the listed FrogPog originals.
"""
import hashlib
import json
from pathlib import Path
import sys
import urllib.request
import numpy as np
import soundfile as sf

root = Path(__file__).resolve().parents[1]
source = Path(sys.argv[1])
packs = {
    'ui': ('Kenney', 'https://kenney.nl/assets/interface-sounds'),
    'impact': ('Kenney', 'https://kenney.nl/assets/impact-sounds'),
    'sci': ('Kenney', 'https://kenney.nl/assets/sci-fi-sounds'),
    'chip': ('FrogPog', 'https://opengameart.org/content/chiptune-sfx-pack'),
}
# cue, pack, original filename, maximum duration, output peak
rows = [
 ('surface/safe_touchdown','impact','impactPunch_heavy_000.ogg',.8,.32),
 ('surface/hard_touchdown','impact','impactPunch_heavy_001.ogg',1.1,.42),
 ('surface/bay_open','sci','doorOpen_000.ogg',.65,.16),
 ('surface/rig_ejection','sci','impactMetal_000.ogg',.35,.18),
 ('surface/arresting_burst','sci','thrusterFire_000.ogg',.3,.16),
 ('surface/rig_impact','impact','impactPunch_heavy_002.ogg',.7,.28),
 ('surface/drone_launch','sci','spaceEngineSmall_000.ogg',.2,.12),
 ('surface/bay_close','sci','doorClose_000.ogg',.65,.16),
 ('surface/surface_ready','chip','pick_up.wav',.8,.17),
 ('surface/takeoff_ignition','sci','spaceEngineLarge_000.ogg',1.3,.42),
 ('ui/focus','ui','tick_001.ogg',.035,.025),
 ('ui/activate','ui','select_001.ogg',.15,.10),
 ('ui/cancel','ui','back_001.ogg',.2,.10),
 ('ui/open','ui','maximize_001.ogg',.25,.10),
 ('ui/close','ui','minimize_001.ogg',.25,.10),
 ('ui/error','chip','fail.wav',.35,.14),
 ('ui/toggle','ui','switch_001.ogg',.15,.10),
 ('gameplay/upgrade','ui','confirmation_001.ogg',.24,.11),
 ('gameplay/reward','chip','level_passed.wav',2.0,.18),
 ('gameplay/ore_credit','ui','click_001.ogg',.065,.07),
 ('gameplay/progression','ui','confirmation_003.ogg',.32,.12),
 ('gameplay/damage','chip','damaged.wav',.35,.22),
 ('gameplay/failure','chip','fail.wav',1.0,.24),
 ('gameplay/warning','ui','error_001.ogg',.4,.16),
 ('gameplay/scanner','sci','computerNoise_000.ogg',.5,.13),
 ('gameplay/tether','sci','forceField_000.ogg',.3,.14),
 ('gameplay/drill','impact','impactMining_000.ogg',.4,.12),
 ('gameplay/deposit','ui','click_001.ogg',.085,.09),
 ('gameplay/repair','ui','confirmation_001.ogg',.4,.14),
 ('gameplay/drone_task','sci','computerNoise_001.ogg',.2,.09),
 ('gameplay/drone_return','ui','confirmation_002.ogg',.25,.10),
 ('gameplay/engine_toggle','sci','thrusterFire_001.ogg',.3,.16),
 ('gameplay/orbit','ui','confirmation_003.ogg',.5,.14),
 ('gameplay/weapon','sci','laserRetro_000.ogg',.15,.12),
 ('gameplay/thrust','sci','spaceEngineLow_000.ogg',2.0,.32),
]
manifest = []
for cue, pack, name, seconds, peak in rows:
    original = source / pack / ('Audio' if pack != 'chip' else '') / name
    if pack == 'chip' and not original.exists():
        original.parent.mkdir(parents=True, exist_ok=True)
        urllib.request.urlretrieve('https://opengameart.org/sites/default/files/' + name, original)
    samples, rate = sf.read(original, always_2d=True)
    samples = samples.mean(axis=1)
    # Remove source leading/trailing silence before applying a short release.
    active = np.flatnonzero(np.abs(samples) > .001)
    if not len(active):
        raise ValueError(f'Silent source: {original}')
    samples = samples[max(0, active[0]-int(rate*.003)):active[-1]+1][:int(rate*seconds)].copy()
    landing = cue in ('surface/safe_touchdown', 'surface/hard_touchdown', 'surface/rig_impact')
    if landing:
        # Slow the recorded impact for weight, then remove its sharp upper edge.
        speed = .5 if cue == 'surface/hard_touchdown' else .6
        samples = np.interp(np.arange(0, len(samples)-1, speed), np.arange(len(samples)), samples)
        width = max(3, int(rate*.002))
        samples = np.convolve(samples, np.ones(width)/width, mode='same')[:int(rate*seconds)]
    if cue == 'ui/focus':
        samples = np.convolve(samples, np.ones(15)/15, mode='same')
    if cue in ('gameplay/ore_credit', 'gameplay/deposit'):
        samples = np.convolve(samples, np.ones(15)/15, mode='same')
    softened = cue in ('gameplay/upgrade', 'gameplay/progression', 'gameplay/drill', 'surface/takeoff_ignition')
    if softened:
        speed = .55 if cue == 'gameplay/drill' else .75
        samples = np.interp(np.arange(0, len(samples)-1, speed), np.arange(len(samples)), samples)
        width = max(3, int(rate * (.0015 if cue == 'gameplay/drill' else .0007)))
        samples = np.convolve(samples, np.ones(width)/width, mode='same')[:int(rate*seconds)]
    if cue == 'gameplay/thrust':
        overlap = min(int(rate*.06), len(samples)//4)
        mix = np.linspace(0, 1, overlap)
        samples = np.concatenate([samples[overlap:-overlap], samples[-overlap:]*(1-mix)+samples[:overlap]*mix])
    samples *= peak / max(np.max(np.abs(samples)), .001)
    fade = min(int(rate*.008), len(samples)//2)
    if cue != 'gameplay/thrust':
        samples[:fade] *= np.linspace(0, 1, fade)
        samples[-fade:] *= np.linspace(1, 0, fade)
        if softened:
            release = min(int(rate*.05), len(samples)//2)
            samples[-release:] *= np.linspace(1, 0, release)
    target = root / 'assets/audio' / (cue + '.wav')
    target.parent.mkdir(parents=True, exist_ok=True)
    sf.write(target, samples, rate, subtype='PCM_16')
    creator, url = packs[pack]
    manifest.append(dict(cue=cue, file=cue+'.wav', creator=creator, source=url,
        original_filename=name, original_sha256=hashlib.sha256(original.read_bytes()).hexdigest(),
        license='CC0-1.0', license_url='https://creativecommons.org/publicdomain/zero/1.0/',
        required_credit=None, processing=('mono, silence trim, duration cap, peak normalization, 60ms loop crossfade' if cue == 'gameplay/thrust'
            else 'mono, silence trim, duration cap, peak normalization, 8ms fades' + ('; 15-sample smoothing' if cue in ('ui/focus', 'gameplay/ore_credit', 'gameplay/deposit') else '')
            + (f'; recorded impact slowed to {speed}x, 2ms moving-average low-pass' if landing else '')
            + (f'; slowed to {speed}x, {width}-sample low-pass, 50ms release' if softened else '')),
        duration=round(len(samples)/rate,4), peak=peak,
        sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
(root/'assets/audio/manifest.json').write_text(json.dumps(manifest, indent=2)+'\n', encoding='utf-8')
print(f'Imported {len(manifest)} CC0 cues')
