#!/usr/bin/env python3
"""Generate the v2 comprehensive test signal for A/B-ing the plugin against the real King of Tone.

v2 (2026-06-27) extends the original layout for the full reference-validation suite. The user
RE-CAPTURES all 25 settings against this signal; the analyzer (analyze.py) and the null
orchestrator (run_validation.py) import SEGMENTS / segment_times() from here so the segment
timings have a single source of truth.

Layout (48 kHz, mono, 32-bit float). Each audio segment is bracketed by silence so segments
auto-detect; analysis keys off the documented timings (segment_times()), robust to the small
plugin/NAM latency by analysing the steady middle of each segment.

  - cal_1k        1 kHz @ -18 dBFS                     level-calibration anchor
  - sweep_clean   log sweep 20->20k @ -30 dBFS         CLEAN linear frequency response (Farina ESS)
  - sweep_drv_-18 / _-12 / _-6  log sweeps @ those dB  DRIVEN: continuous THD-vs-freq (Farina ESS)
  - lvl_<dB>      1 kHz steps -30..-3 dBFS (3 dB)       gain/compression vs input level
  - tone_<f>      tones @ -14 dBFS, dense 1-8 kHz       harmonics vs frequency (spot checks)
  - imd_smpte     60 Hz + 7 kHz (4:1)                   SMPTE intermodulation distortion
  - imd_ccif      19 k + 20 k (1:1)                     CCIF (twin-tone) intermodulation
  - decay_220 / decay_1k  plucked exp-decay notes       touch-sensitivity / dynamic response

The log sweeps are true exponential sine sweeps (ESS): instantaneous freq f(t)=f0*exp(t/T*k),
k=ln(f1/f0). analyze.py builds the matching Farina inverse filter to deconvolve them into the
linear impulse response plus separated harmonic-order responses.
"""
import numpy as np

FS = 48000

# Sweep parameters — shared with the analyzer's Farina inverse filter.
SWEEP_F0 = 20.0
SWEEP_F1 = 20000.0
SWEEP_CLEAN_SEC = 10.0
SWEEP_DRIVEN_SEC = 8.0
DRIVEN_LEVELS_DB = (-18, -12, -6)

# Discrete tone frequencies (Hz) @ -14 dBFS — densified through the 1-8 kHz saturation region.
TONE_FREQS = (82.41, 110, 220, 440, 1000, 1500, 2000, 3000, 4000, 5000, 6000, 8000)

# 1 kHz level steps (dBFS) for the compression curve.
LEVEL_STEPS_DB = tuple(range(-30, -2, 3))   # -30,-27,...,-3

GAP = 0.3          # silence between segments (s)
TONE_SEC = 0.8     # discrete tone / level-step duration (s)


def dbfs(db):
    return 10.0 ** (db / 20.0)


def silence(sec):
    return np.zeros(int(sec * FS), dtype=np.float64)


def fade(x, ms=5.0):
    n = int(ms * 1e-3 * FS)
    if n * 2 < len(x):
        r = np.linspace(0, 1, n)
        x[:n] *= r
        x[-n:] *= r[::-1]
    return x


def tone(freq, sec, db):
    t = np.arange(int(sec * FS)) / FS
    return fade(dbfs(db) * np.sin(2 * np.pi * freq * t))


def log_sweep(sec, db, f0=SWEEP_F0, f1=SWEEP_F1):
    """True exponential sine sweep (Farina ESS)."""
    t = np.arange(int(sec * FS)) / FS
    T = sec
    k = np.log(f1 / f0)
    phase = 2 * np.pi * f0 * T / k * (np.exp(t / T * k) - 1.0)
    return fade(dbfs(db) * np.sin(phase), 10.0)


def twin_tone(f_lo, f_hi, sec, db_peak, ratio_lo=1.0, ratio_hi=1.0):
    """Two summed tones, scaled so the peak sits at db_peak (amplitudes in ratio_lo:ratio_hi)."""
    t = np.arange(int(sec * FS)) / FS
    a_lo = ratio_lo / (ratio_lo + ratio_hi)
    a_hi = ratio_hi / (ratio_lo + ratio_hi)
    x = a_lo * np.sin(2 * np.pi * f_lo * t) + a_hi * np.sin(2 * np.pi * f_hi * t)
    x *= dbfs(db_peak) / (np.max(np.abs(x)) + 1e-20)
    return fade(x)


def plucked(freq, sec, db_peak, decay_tau=0.35):
    """A plucked note: fast attack, exponential decay — sweeps DOWN through the drive range as it
    decays, so analysis can track harmonic content vs instantaneous level (touch-sensitivity)."""
    t = np.arange(int(sec * FS)) / FS
    env = np.exp(-t / decay_tau)
    atk = int(0.003 * FS)
    env[:atk] *= np.linspace(0, 1, atk)
    return dbfs(db_peak) * env * np.sin(2 * np.pi * freq * t)


def build_segments():
    """Return the ordered list of (name, audio_array). Pure data — no I/O."""
    segs = []
    segs.append(("cal_1k", tone(1000, 1.0, -18)))
    segs.append(("sweep_clean", log_sweep(SWEEP_CLEAN_SEC, -30)))
    for db in DRIVEN_LEVELS_DB:
        segs.append((f"sweep_drv_{db}", log_sweep(SWEEP_DRIVEN_SEC, db)))
    for db in LEVEL_STEPS_DB:
        segs.append((f"lvl_{db}", tone(1000, TONE_SEC, db)))
    for f in TONE_FREQS:
        segs.append((f"tone_{f:g}", tone(f, TONE_SEC, -14)))
    segs.append(("imd_smpte", twin_tone(60, 7000, 2.0, -12, ratio_lo=4.0, ratio_hi=1.0)))
    segs.append(("imd_ccif", twin_tone(19000, 20000, 2.0, -12)))
    segs.append(("decay_220", plucked(220, 1.5, -6)))
    segs.append(("decay_1k", plucked(1000, 1.5, -6)))
    return segs


def assemble(lead=0.5, gap=GAP, tail=0.5):
    """Concatenate segments with `lead`/`gap`/`tail` silence; return (signal, timing_map).

    timing_map[name] = (t0, t1) bounds the AUDIO of that segment in seconds.
    """
    parts = [silence(lead)]
    pos = lead
    times = {}
    for name, audio in build_segments():
        t0 = pos
        parts.append(audio)
        pos += len(audio) / FS
        times[name] = (t0, pos)
        parts.append(silence(gap))
        pos += gap
    parts.append(silence(tail - gap if tail > gap else 0.0))
    sig = np.concatenate(parts).astype(np.float32)
    return sig, times


def segment_times():
    """Timing map only (no array allocation cost beyond building segments)."""
    return assemble()[1]


if __name__ == "__main__":
    from scipy.io import wavfile

    sig, times = assemble()
    out = "analysis/test_signal_48k.wav"
    wavfile.write(out, FS, sig)
    peak = float(np.max(np.abs(sig)))
    print(f"wrote {out}  ({len(sig)/FS:.1f} s, {len(sig)} samples, {FS} Hz)")
    print(f"peak = {peak:.3f}  ({20*np.log10(peak+1e-20):.1f} dBFS)")
    print("\nsegment timing map (s):")
    for name, (t0, t1) in times.items():
        print(f"  {name:16} {t0:7.3f} .. {t1:7.3f}  ({t1-t0:.2f}s)")
