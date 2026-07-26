#!/usr/bin/env python3
"""Capture I/O and render-argument mapping for Monarch of Tone (King of Tone clone).

Implements the interface analysis/comprehensive_report.py expects — find_captures(),
load_capture(), render_args(), RENDER_BIN — imported from the Guitar-Pedal-Plugin-Template
harness (see that project's analysis/README.md for the general contract).

Captures live in analysis/pedal_export2/ (44 real-pedal NAM captures of a real King of Tone,
local-only/gitignored — see CLAUDE.md "Real-Pedal Calibration Harness"), Yellow channel only,
named "G<drive> T<tone> <Mode> ...wav" where drive/tone are on a 0-10 dial scale and Mode is
Clean/OD/Dist (the convention run_validation.py's parse_settings() already uses). PedalRender
renders Yellow-only (Red bypassed) at fixed volume=0.5, presence=0.0 to match the captured knob
positions — see tools/PedalRender.cpp.
"""
import glob
import os
import re

import numpy as np
from scipy.io import wavfile
from scipy import signal as sps

RENDER_BIN = "build/PedalRender_artefacts/Release/PedalRender"
CAPTURE_DIR = "analysis/pedal_export2"

# Filename mode token -> (clipping_mode int, display label). "rev" is repurposed here as the
# clipping mode (this project has one plugin revision, not several) so the dashboard's per-"rev"
# tiles/charts read as per-mode headlines, matching the Boost/OD/Dist breakdown CLAUDE.md reports.
_MODE_TOKENS = {
    "clean": (0, "Boost"), "boost": (0, "Boost"),
    "od": (1, "Overdrive"), "overdrive": (1, "Overdrive"),
    "dist": (2, "Distortion"), "distortion": (2, "Distortion"),
}
_FNAME_RE = re.compile(r"G([\d.]+)\s*T([\d.]+)\s*(Clean|OD|Overdrive|Dist|Distortion|Boost)",
                       re.IGNORECASE)

# OS factor (1/2/4/8, as passed on comprehensive_report.py's --os) -> PedalRender's osIndex arg.
OS_FACTOR_TO_INDEX = {1: 0, 2: 1, 4: 2, 8: 3}


def parse_capture(filename):
    """'G6 T5 OD ...wav' -> {"rev": "Overdrive", "drive": 0.6, "tone": 0.5, "clip": 1, "label": ...}."""
    base = os.path.basename(filename)
    m = _FNAME_RE.match(base)
    if not m:
        raise ValueError(f"unparseable capture filename: {base}")
    g_raw, t_raw, mode_tok = m.group(1), m.group(2), m.group(3)
    clip, rev = _MODE_TOKENS[mode_tok.lower()]
    return {
        "rev": rev,
        "drive": float(g_raw) / 10.0,
        "tone": float(t_raw) / 10.0,
        "clip": clip,
        "label": f"G{g_raw} T{t_raw} {mode_tok}",
    }


def find_captures(directory=CAPTURE_DIR):
    """Return sorted [(path, parsed_dict), ...] for every .wav under directory."""
    if not os.path.isdir(directory):
        return []
    return [(p, parse_capture(p)) for p in sorted(glob.glob(os.path.join(directory, "*.wav")))]


def load_capture(path, expect_fs=48000):
    """Load a capture as float64 mono at `expect_fs`.

    Some NAM modelers export 44.1 kHz audio inside a 48 kHz-labeled WAV. This detects the speed
    error from the cal_1k tone (~1088 Hz on a mislabeled file, at the lead-in + tone placement
    gen_test_signal.py uses) and resamples back to `expect_fs`. A correctly-labeled file passes
    through untouched."""
    sr, x = wavfile.read(path)
    if x.dtype.kind in "iu":
        x = x.astype(np.float64) / np.iinfo(x.dtype).max
    else:
        x = x.astype(np.float64)
    if x.ndim > 1:
        x = x.mean(axis=1)

    cal_win = (0.5, 1.45)   # gen_test_signal's cal_1k: 0.5s lead-in, 1.0s tone
    seg = x[int(cal_win[0] * sr):int(cal_win[1] * sr)]
    if len(seg) > 64:
        w = np.hanning(len(seg))
        mag = np.abs(np.fft.rfft(seg * w))
        peak_hz = np.fft.rfftfreq(len(seg), 1.0 / sr)[int(np.argmax(mag))]
        ratio = peak_hz / 1000.0
    else:
        ratio = 1.0

    _COMMON_RATES = (44100, 48000, 88200, 96000)
    if abs(ratio - 1.0) > 0.005:
        est = sr / ratio
        true_rate = min(_COMMON_RATES, key=lambda r: abs(r - est))
        x = sps.resample_poly(x, expect_fs, true_rate)
    elif sr != expect_fs:
        x = sps.resample_poly(x, expect_fs, sr)

    return np.asarray(x, dtype=np.float64)


def render_args(parsed, extra_args=None):
    """Parsed settings -> the 5 fixed positional args PedalRender takes after in/out paths:
    drive tone vol pres clip. Volume/presence are pinned to the captured knob positions (0.5/0.0),
    matching run_validation.py's existing convention."""
    args = [f"{parsed['drive']:.4f}", f"{parsed['tone']:.4f}", "0.5000", "0.0000", str(parsed["clip"])]
    if extra_args:
        args += list(extra_args)
    return args


if __name__ == "__main__":
    caps = find_captures()
    print(f"{len(caps)} captures in {CAPTURE_DIR}/")
    for path, d in caps:
        print(f"  {os.path.basename(path)}  ->  {d}")
