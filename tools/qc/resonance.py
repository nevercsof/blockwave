#!/usr/bin/env python3
"""BLOCKWAVE resonance-harshness QC.

Stdlib only (same rules as tools/qc/renderpack.py, whose WAV IO and
render-invocation patterns are reused).  Detects what renderpack.py is blind
to: a sustained sound that is PIERCING rather than merely bright.

Full findings, thresholds and the bank audit:
CHECKPOINTS/artifacts/RESONANCE_QC.md   (numbers: RESONANCE_QC.csv)

--------------------------------------------------------------------------
WHY THE OBVIOUS METRIC DOES NOT WORK
--------------------------------------------------------------------------
"Max bin minus smoothed spectrum" is useless on this synth.  Every voice is
a square/pulse: the spectrum IS a comb of narrow lines.  Any bin-domain
prominence measure reports 15-25 dB for a perfectly innocent square, because
the smoothing window averages loud harmonic bins with empty bins between
them.  Pulse width adds another trap: a 50 % square has NO even harmonics
(~40 dB down), so "harmonic 3 versus harmonic 2" is +40 dB on a clean sound.

So peakiness is measured on a *harmonic upper envelope* rather than on raw
bins, prominence is topographic (must protrude above BOTH sides), and the
primary detector is psychoacoustic sharpness rather than peakiness at all --
see the note on PROM's sensitivity in step 5.

--------------------------------------------------------------------------
METHOD
--------------------------------------------------------------------------
1. RENDER.  The real recipe-aware path:
       render <preset.json> note:<N>:2s <out.wav> --sr 48000
   craft grid -> recipe override -> params, exactly what the plugin runs.
   Every preset is rendered across its register (see PITCHES): a patch can be
   fine at C3 and vicious at C6, so a single-pitch check proves nothing.

2. SUSTAIN WINDOW (never the attack).  A 10 ms RMS envelope is built; the
   window starts 100 ms after the envelope peak (peak searched only in the
   first 1.5 s so slow CLOUD/PAD attacks do not push it into the release)
   and ends where the envelope has fallen 30 dB, capped at note-off.  Short
   sounds fall back to a >= 150 ms window just after the transient.  This
   measures the RINGING, not the click.

3. SPECTRUM.  Welch: Hann, N = 8192 (5.86 Hz bins at 48 kHz -- fine enough
   to resolve C2's 65 Hz harmonic spacing), 50 % overlap, up to 8 frames,
   averaged power, computed on the mono mid (L+R)/2.  The per-voice filter
   is identical on L and R, so the mid loses nothing relevant.

4. SHARP (acum) -- THE PRIMARY DETECTOR.  Zwicker/DIN 45692 sharpness,
   simplified: the centroid of specific loudness weighted by g(z), which
   climbs steeply above 15.8 Bark (~3.1 kHz).  It is the standard descriptor
   for exactly the complaint being investigated, and being a ratio it is
   level-independent -- it separates "piercing" from "loud".  See the
   sharpness() docstring for the simplifications.

5. PROM (dB) -- SPECTRAL PEAKINESS, on a harmonic upper envelope:
       E(f)    = max power over bins within +/- 1.05 * f0 of f
       prom(f) = E_dB(f) - max( median E_dB over [f/2.0, f/1.4],
                                median E_dB over [f*1.4, f*2.0] )
   The envelope window is 2.1 * f0 wide, i.e. guaranteed to contain at least
   one ODD harmonic (odd harmonics are spaced 2 * f0), so E tracks the local
   harmonic amplitude envelope and is immune both to the comb structure and
   to the missing evens of a 50 % square.  f0 is known exactly (we chose the
   note), which is what makes this possible.  Each side level is a median of
   3 block medians, which survives the periodic sinc nulls of narrow pulse
   widths.  E is clamped 80 dB below its max: without that guard a
   neighbourhood driven to float32 zero makes prominence unbounded.

   Taking the MAX of the two side levels (not the mean, not the min) is the
   crux: it makes a filter KNEE score ~0 dB.  A non-resonant LP24 at 4 kHz is
   flat below and -24 dB/oct above, so the low side already sits at the
   peak's level.  Only a hump protruding above BOTH neighbourhoods scores.

   SENSITIVITY WARNING, measured on the calibration sweep: driving filt_res
   from 0.10 to 1.00 (a +22 dB filter peak) moves PROM by only ~4 dB, because
   a lowpass resonance sits on the shoulder of its own skirt and on the
   source's -6 dB/oct tilt, both of which lift the comparison neighbourhood.
   PROM reliably finds narrow FIXED peaks (crush alias images, noise-band
   rings); it is not a resonance meter in isolation.

6. CANDIDATE GATE for PROM.  A hump only counts if it is
       f >= 3 * f0        (a hump under the 3rd harmonic is tone colour, not
                           a separate piercing band),
       1200 Hz <= f <= 9000 Hz    (the ear-sensitive / "piercing" region),
       E_dB(f) >= max(E_dB) - 30 dB   (audible: a 20 dB hump 45 dB below the
                                       fundamental is inaudible ripple).

7. SUPPORTING NUMBERS (same window, same spectrum):
       HF8K  = energy above 8 kHz / energy above 20 Hz, in %.  Catches RAW
               aliasing hash and crush grit -- broadband, invisible to PROM.
       PAIN  = energy in 2-6 kHz / total, in %.  Reported for context only,
               NOT thresholded: above C5 it mostly measures "the fundamental
               is now in the 2-6 kHz band" rather than any defect.
       K_db  = BS.1770 K-weighted level of the window, a loudness yardstick
               for checking whether the bank is matched by ear or by RMS.

8. CAUSAL ABLATION.  Every measurement can be repeated with one parameter
   neutralised (filt_res, raw, crush_mix, keytrack, ...).  The override goes
   into "params", which render applies AFTER the craft grid -- i.e. exactly
   the edit a preset fix would make -- so an ablation both attributes the
   cause and predicts what the fix will sound like.  Nothing under presets/
   is ever touched.  --scan runs the filt_res ablation over the whole bank
   (RESLIFT_db column); --probe runs the full set on one preset.

9. PEAK-MOTION TEST.  A peak pinned to a FIXED Hz across the register is a
   whistle sitting on top of the note (filter resonance, crush image, noise
   ring) -- the genuinely "resonant" defect.  A peak whose frequency scales
   with the note is source structure (pulse-width comb lobe, sync formant),
   i.e. timbre.  Only the former is flagged.

Calibration (--calibrate) renders synthetic controls -- a bare LEAD voice with
filt_res swept 0.10 -> 1.00 across pulse widths, unison, noise, filter types,
cutoff placements and notes -- so the thresholds are anchored to a known
ground truth instead of guessed.

Usage:
    resonance.py --calibrate            synthetic ground-truth sweep
    resonance.py --scan                 all of presets/factory, CSV + summary
    resonance.py --probe NAME [NAME..]  detail + ablation A/B for one preset
"""
import csv
import json
import math
import os
import statistics
import struct
import subprocess
import sys

ROOT = "/Users/kirillboyko/Downloads/blockwave-kickoff"
RENDER = f"{ROOT}/build/release/render_artefacts/Release/render"
FACTORY = f"{ROOT}/presets/factory"
OUTDIR = f"{ROOT}/CHECKPOINTS/artifacts"
TMP = os.environ.get("BW_QC_TMP", "/tmp/bw_resonance")
os.makedirs(TMP, exist_ok=True)

SR = 48000
NFFT = 8192
MAX_FRAMES = 8

# Pitches per category.  Melodic categories are checked across four octaves
# (C2..C6) because the suspect mechanism only bites high up.  BASS and FX are
# checked over their real playable span; PERC stays in its zone (SOUND_DESIGN
# exempts PERC from the 2-octave rule).
PITCHES = {
    "BASS": ["C1", "C2", "C3", "C4"],
    "PERC": ["C2", "C3", "C4"],
    "FX":   ["C2", "C3", "C4"],
}
PITCHES_DEFAULT = ["C2", "C3", "C4", "C5", "C6"]

NOTE_SEMI = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def note_hz(name):
    semi = NOTE_SEMI[name[0].upper()]
    i = 1
    if name[i] == "#":
        semi += 1
        i += 1
    midi = (int(name[i:]) + 1) * 12 + semi
    return 440.0 * (2.0 ** ((midi - 69) / 12.0))


# --------------------------------------------------------------------------
# WAV IO (same RIFF walker as renderpack.py: handles JUNK chunks, f32 and s16)
# --------------------------------------------------------------------------
def read_wav(path):
    with open(path, "rb") as f:
        b = f.read()
    assert b[:4] == b"RIFF" and b[8:12] == b"WAVE", path
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(b):
        cid, sz = b[pos:pos + 4], struct.unpack("<I", b[pos + 4:pos + 8])[0]
        body = b[pos + 8:pos + 8 + sz]
        if cid == b"fmt ":
            fmt = body
        elif cid == b"data":
            data = body
        pos += 8 + sz + (sz & 1)
    tag, ch, sr = struct.unpack("<HHI", fmt[:8])
    bits = struct.unpack("<H", fmt[14:16])[0]
    if tag == 3 and bits == 32:
        smp = struct.unpack(f"<{len(data)//4}f", data)
    elif tag == 1 and bits == 16:
        smp = [s / 32768.0 for s in struct.unpack(f"<{len(data)//2}h", data)]
    else:
        raise ValueError(f"unsupported wav fmt {tag}/{bits}")
    if ch == 1:
        mono = list(smp)
    else:
        mono = [(smp[i] + smp[i + 1]) * 0.5 for i in range(0, len(smp) - 1, ch)]
    return sr, mono


# --------------------------------------------------------------------------
# FFT: iterative radix-2 + real-input trick (two real halves in one complex
# transform).  Pure python, ~80 ms per 8192-point real frame.
# --------------------------------------------------------------------------
_TW = {}


def _twiddles(n):
    tw = _TW.get(n)
    if tw is None:
        tw = []
        m = 2
        while m <= n:
            half = m >> 1
            tw.append([complex(math.cos(-2 * math.pi * j / m),
                               math.sin(-2 * math.pi * j / m)) for j in range(half)])
            m <<= 1
        _TW[n] = tw
    return tw


def fft(a):
    n = len(a)
    j = 0
    for i in range(1, n):
        bit = n >> 1
        while j & bit:
            j ^= bit
            bit >>= 1
        j |= bit
        if i < j:
            a[i], a[j] = a[j], a[i]
    tw = _twiddles(n)
    m, stage = 2, 0
    while m <= n:
        half = m >> 1
        w = tw[stage]
        for start in range(0, n, m):
            for k in range(half):
                u = a[start + k]
                v = a[start + k + half] * w[k]
                a[start + k] = u + v
                a[start + k + half] = u - v
        m <<= 1
        stage += 1
    return a


def rfft_power(x):
    """Power spectrum (len n/2+1) of a real sequence of length n (power of 2).
    Uses the pack-real-into-complex trick: half the work of a full complex FFT."""
    n = len(x)
    h = n // 2
    a = [complex(x[2 * i], x[2 * i + 1]) for i in range(h)]
    fft(a)
    out = [0.0] * (h + 1)
    a.append(a[0])
    for k in range(h + 1):
        e = (a[k] + a[h - k].conjugate()) * 0.5
        o = (a[k] - a[h - k].conjugate()) * complex(0, -0.5)
        w = complex(math.cos(-2 * math.pi * k / n), math.sin(-2 * math.pi * k / n))
        c = e + w * o
        out[k] = c.real * c.real + c.imag * c.imag
    return out


_HANN = {}


def hann(n):
    w = _HANN.get(n)
    if w is None:
        w = [0.5 - 0.5 * math.cos(2 * math.pi * i / n) for i in range(n)]
        _HANN[n] = w
    return w


def db(x, floor=-200.0):
    return floor if x <= 1e-20 else 10.0 * math.log10(x)


# --------------------------------------------------------------------------
# Sustain window
# --------------------------------------------------------------------------
def sustain_window(x, sr, note_len=2.0):
    """(start, end) sample indices of the ringing part, or None if silent."""
    blk = int(0.010 * sr)
    nb = len(x) // blk
    if nb < 8:
        return None
    env = []
    for i in range(nb):
        s = x[i * blk:(i + 1) * blk]
        env.append(math.sqrt(sum(v * v for v in s) / blk))
    peak = max(env)
    if peak < 10 ** (-60 / 20.0):
        return None
    lim = min(nb, int(1.5 / 0.010))            # peak searched in the first 1.5 s
    pk_i = max(range(lim), key=lambda i: env[i])
    start_i = pk_i + 10                        # +100 ms: past the transient
    end_cap = min(nb, int(note_len / 0.010))   # never cross note-off
    thr = peak * 10 ** (-30 / 20.0)
    end_i = start_i
    while end_i < end_cap and env[end_i] >= thr:
        end_i += 1
    if end_i - start_i < 25:                   # short/percussive fallback
        start_i = pk_i + 3
        end_i = start_i
        while end_i < end_cap and env[end_i] >= peak * 10 ** (-40 / 20.0):
            end_i += 1
        if end_i - start_i < 15:
            end_i = min(end_cap, start_i + 15)
    if end_i - start_i < 10:
        return None
    a, b = start_i * blk, min(len(x), end_i * blk)
    seg = x[a:b]
    if math.sqrt(sum(v * v for v in seg) / max(1, len(seg))) < 10 ** (-60 / 20.0):
        return None
    return a, b


def welch(x, a, b):
    """Averaged power spectrum over x[a:b]."""
    w = hann(NFFT)
    frames, pos = [], a
    while pos + NFFT <= b and len(frames) < MAX_FRAMES:
        frames.append(pos)
        pos += NFFT // 2
    if not frames:
        if b - a < NFFT // 4:
            return None
        seg = list(x[a:b]) + [0.0] * (NFFT - (b - a))   # zero-pad short windows
        return rfft_power([seg[i] * w[i] for i in range(NFFT)])
    acc = [0.0] * (NFFT // 2 + 1)
    for p in frames:
        sp = rfft_power([x[p + i] * w[i] for i in range(NFFT)])
        for k in range(len(acc)):
            acc[k] += sp[k]
    n = len(frames)
    return [v / n for v in acc]


# --------------------------------------------------------------------------
# The metric
# --------------------------------------------------------------------------
GRID_LO, GRID_HI, GRID_PPO = 120.0, 21000.0, 48
GRID_N = int(GRID_PPO * math.log2(GRID_HI / GRID_LO)) + 1
GRID_F = [GRID_LO * 2 ** (i / GRID_PPO) for i in range(GRID_N)]
SIDE_IN = int(round(GRID_PPO * math.log2(1.40)))    # ~1/2-octave dead zone
SIDE_OUT = int(round(GRID_PPO * math.log2(2.00)))   # 1-octave outer edge
NBLK = 3                                            # blocks per side (median of)

CAND_LO_HZ, CAND_HI_HZ = 1200.0, 9000.0
CAND_MIN_HARM = 3.0        # candidate must sit at or above the 3rd harmonic
REL_GATE_DB = -30.0        # hump must be within 30 dB of the loudest partial


def _sidelevel(edb, a, b):
    """Robust level of a neighbourhood: median of NBLK block medians.
    Block-then-median survives the periodic sinc nulls of narrow pulse widths,
    which a plain median over a whole side does not."""
    L = b - a
    v = []
    for j in range(NBLK):
        s = a + (L * j) // NBLK
        e = a + (L * (j + 1)) // NBLK
        if e > s:
            v.append(statistics.median(edb[s:e]))
    return statistics.median(v)


# --- psychoacoustic sharpness (Zwicker/DIN 45692, simplified) --------------
# "Остро" is literally sharpness: the standard psychoacoustic descriptor for
# piercing timbre.  It is the centroid of specific loudness weighted by g(z),
# which climbs steeply above 15.8 Bark (~3.1 kHz) -- the ear's pain region.
# Simplifications (documented, they matter for absolute acum but not for
# ranking): excitation = band energy through Terhardt's outer/middle-ear
# transfer a0(f); specific loudness = E^0.23 (Stevens) instead of a full
# Zwicker loudness model; no upward spread of masking.  Sharpness is a ratio,
# so it is level-independent -- it separates "piercing" from "loud".
BARK_STEP = 0.5
NBARK = int(24.0 / BARK_STEP)


def _bark(f):
    return 13.0 * math.atan(0.00076 * f) + 3.5 * math.atan((f / 7500.0) ** 2)


def _a0_db(f):
    """Terhardt outer+middle ear transfer, dB."""
    k = max(f, 20.0) / 1000.0
    return (-3.64 * k ** -0.8
            + 6.5 * math.exp(-0.6 * (k - 3.3) ** 2)
            - 1e-3 * k ** 3.6)


def _gz(z):
    return 1.0 if z < 15.8 else 0.066 * math.exp(0.171 * z)


_EAR_CACHE = {}


def _ear_tables(sr, nb):
    t = _EAR_CACHE.get((sr, nb))
    if t is None:
        binhz = sr / NFFT
        idx, gain = [], []
        for k in range(nb):
            f = k * binhz
            idx.append(min(NBARK - 1, int(_bark(f) / BARK_STEP)) if f >= 20 else -1)
            gain.append(10.0 ** (_a0_db(f) / 10.0) if f >= 20 else 0.0)
        # BS.1770 K-weighting magnitude^2 at 48 kHz (frequency-domain form of
        # the two standard biquads) -- a loudness yardstick to check whether
        # the bank is matched by ear or only by RMS.
        kw = []
        b1 = (1.53512485958697, -2.69169618940638, 1.19839281085285)
        a1 = (1.0, -1.69065929318241, 0.73248077421585)
        b2 = (1.0, -2.0, 1.0)
        a2 = (1.0, -1.99004745483398, 0.99007225036621)
        for k in range(nb):
            w = 2.0 * math.pi * (k * binhz) / sr
            z1 = complex(math.cos(-w), math.sin(-w))
            z2 = z1 * z1
            h1 = (b1[0] + b1[1] * z1 + b1[2] * z2) / (a1[0] + a1[1] * z1 + a1[2] * z2)
            h2 = (b2[0] + b2[1] * z1 + b2[2] * z2) / (a2[0] + a2[1] * z1 + a2[2] * z2)
            h = h1 * h2
            kw.append(h.real * h.real + h.imag * h.imag)
        t = (idx, gain, kw)
        _EAR_CACHE[(sr, nb)] = t
    return t


def sharpness(spec, sr):
    """-> (acum, K-weighted level dB)."""
    nb = len(spec)
    idx, gain, kw = _ear_tables(sr, nb)
    bands = [0.0] * NBARK
    kws = 0.0
    for k in range(1, nb):
        i = idx[k]
        if i >= 0:
            bands[i] += spec[k] * gain[k]
        kws += spec[k] * kw[k]
    num = den = 0.0
    for i, e in enumerate(bands):
        if e <= 0.0:
            continue
        z = (i + 0.5) * BARK_STEP
        n = e ** 0.23
        num += n * _gz(z) * z
        den += n
    acum = 0.11 * num / den * BARK_STEP / 0.5 if den > 0 else 0.0
    return acum, db(kws)


def analyse(spec, sr, f0):
    """-> dict(res, res_hz, rel, hf8k, pain, sharp, kdb) or None."""
    binhz = sr / NFFT
    nb = len(spec)
    half = 1.05 * f0                       # envelope half-window, >= one odd harm.
    env = []
    for f in GRID_F:
        k0 = max(1, int((f - half) / binhz))
        k1 = min(nb - 1, int((f + half) / binhz) + 1)
        if k1 <= k0:
            k0 = min(nb - 1, max(1, int(f / binhz)))
            k1 = k0 + 1
        env.append(max(spec[k0:k1 + 1]))
    edb = [db(v) for v in env]
    emax = max(edb)
    # Clamp the envelope to 80 dB below the loudest partial.  Without this a
    # neighbourhood that is numerically empty (a steep filter can drive a band
    # to float32 zero) makes prominence unbounded -- a 50 % square, whose even
    # harmonics are absent, produced 60 dB readings before this guard.
    floor = emax - 80.0
    edb = [e if e > floor else floor for e in edb]

    best = (-99.0, 0.0, -99.0)
    lo_hz = max(CAND_LO_HZ, CAND_MIN_HARM * f0)
    for i in range(SIDE_OUT, GRID_N - SIDE_OUT):
        f = GRID_F[i]
        if f < lo_hz or f > CAND_HI_HZ:
            continue
        rel = edb[i] - emax
        if rel < REL_GATE_DB:
            continue
        low = _sidelevel(edb, i - SIDE_OUT, i - SIDE_IN + 1)
        high = _sidelevel(edb, i + SIDE_IN, i + SIDE_OUT + 1)
        prom = edb[i] - max(low, high)
        if prom > best[0]:
            best = (prom, f, rel)

    tot = hf = pain = 0.0
    for k in range(1, nb):
        f = k * binhz
        if f < 20.0:
            continue
        p = spec[k]
        tot += p
        if f > 8000.0:
            hf += p
        if 2000.0 <= f <= 6000.0:
            pain += p
    acum, kdb = sharpness(spec, sr)
    return dict(res=round(best[0], 2), res_hz=round(best[1], 1),
                rel=round(best[2], 2),
                hf8k=round(100.0 * hf / tot, 3) if tot > 0 else 0.0,
                pain=round(100.0 * pain / tot, 3) if tot > 0 else 0.0,
                sharp=round(acum, 3), kdb=round(kdb, 2))


def measure(preset_path, note, tag):
    """Render one preset at one note and analyse it."""
    wav = f"{TMP}/{tag}.wav"
    r = subprocess.run([RENDER, preset_path, f"note:{note}:2s", wav,
                        "--sr", str(SR)], capture_output=True, text=True)
    if r.returncode != 0 or not os.path.exists(wav):
        return None, f"render fail: {(r.stderr or '')[:100]}"
    sr, x = read_wav(wav)
    win = sustain_window(x, sr)
    if win is None:
        return None, "no sustain (silent or fully decayed)"
    spec = welch(x, win[0], win[1])
    if spec is None:
        return None, "window too short"
    m = analyse(spec, sr, note_hz(note))
    m["win_ms"] = round((win[1] - win[0]) * 1000.0 / sr, 1)
    return m, ""


def dump_params(preset_path):
    wav = f"{TMP}/_dump.wav"
    r = subprocess.run([RENDER, preset_path, "note:C4:0.05s", wav,
                        "--sr", str(SR), "--dump"], capture_output=True, text=True)
    out = {}
    for line in r.stdout.splitlines():
        p = line.split()
        if len(p) == 2:
            try:
                out[p[0]] = float(p[1])
            except ValueError:
                pass
    return out


# --------------------------------------------------------------------------
# Ablation: re-render the same preset with one parameter neutralised.
# The overrides go into "params", which the render tool applies AFTER the
# craft grid -- i.e. exactly the edit a preset fix would make -- so an
# ablation both attributes the cause and predicts what the fix will sound
# like.  Nothing under presets/ is touched; the variant lands in TMP.
# --------------------------------------------------------------------------
def write_temp_preset(name, category, base, cells, params):
    p = {"formatVersion": 1, "name": name, "category": category,
         "craft": {"base": base, "cells": cells}, "params": params}
    path = f"{TMP}/{name}.json"
    with open(path, "w") as f:
        json.dump(p, f)
    return path


def variant_path(src_path, overrides, tag):
    with open(src_path) as f:
        js = json.load(f)
    js.setdefault("params", {}).update(overrides)
    js["name"] = f"{js.get('name','X')}__{tag}"
    path = f"{TMP}/var_{tag}.json"
    with open(path, "w") as f:
        json.dump(js, f)
    return path


ABL_RES = {"filt_res": 0.12}          # k = 1.77, Q = 0.56: audibly un-resonant
ABL_KT = {"filt_keytrack": 0.6}       # does keytracking alone rescue it?
ABL_RAW = {"raw": 0}
ABL_CRUSH = {"crush_mix": 0.0}


def lift_db(a, b):
    """dB by which quantity a exceeds reference b (shares in %)."""
    if b is None or a is None or b <= 1e-4:
        return 99.0 if (a or 0) > 1e-3 else 0.0
    return round(10.0 * math.log10(max(a, 1e-6) / b), 2)


# --------------------------------------------------------------------------
# FLAG RULE.  Thresholds come from the calibration sweep plus the observed
# distribution over the 128-preset bank; see CHECKPOINTS/artifacts/
# RESONANCE_QC.md for the distributions they were read off.
#
#   F1 SHARP   SHARP >= SHARP_ABS and SHARP - median(bank at the same note)
#              >= SHARP_EXC.  Psychoacoustic sharpness is the direct
#              correlate of the complaint.  Both gates are needed: the
#              absolute one because sharpness climbs with register for every
#              patch, the excess one because it must be sharp *relative to
#              the bank you are auditioning it against*.
#   F2 FIXPEAK PROM >= PROM_GATE and the peak frequency does NOT move with
#              the keyboard.  This is the real "resonance" signature: a
#              narrow band pinned to a fixed Hz (filter resonance, crush
#              alias image, noise-band ring) reads as a whistle sitting on
#              top of the note.  A peak that scales with pitch is source
#              structure -- the pulse-width comb lobe or a sync formant --
#              and is timbre, not a defect.  Verified by ablation: forcing
#              oscA_pw to 50 % collapses the tracking peaks and leaves the
#              fixed ones untouched.  'mixed' motion is treated as source
#              structure (not flagged); 'n/a' -- a peak seen at only one
#              pitch -- is flagged, because a peak that exists at one note
#              and not its octave cannot be following the keyboard.
#   F3 HASH    HF8K >= HF8K_GATE: the sustain is mostly top-octave noise.
# --------------------------------------------------------------------------
SHARP_ABS = 2.30        # acum; bank p95 at C4
SHARP_EXC = 0.70        # acum over the bank median at the same note (p95)
PROM_GATE = 6.0         # dB topographic prominence
REL_GATE_FIX = -30.0    # the fixed peak must be audible
HF8K_GATE = 20.0        # % of sustain energy above 8 kHz
TRACK_TOL = 0.35        # relative tolerance for "peak moved with the note"


def peak_motion(rows):
    """Does the prominent peak track the keyboard?  Compares PROM_hz between
    pitches an octave apart (all PITCHES lists step by one octave) for the
    rows where a peak was actually found.  -> 'tracks' | 'fixed' | 'mixed'."""
    pts = [r for r in rows if r.get("PROM_db") is not None
           and r["PROM_db"] >= 3.0 and r.get("PROM_hz")]
    if len(pts) < 2:
        return "n/a"
    votes = []
    for a, b in zip(pts, pts[1:]):
        ratio = b["PROM_hz"] / a["PROM_hz"]
        octaves = NOTE_ORDER.index(b["note"]) - NOTE_ORDER.index(a["note"])
        if octaves <= 0:
            continue
        expect = 2.0 ** octaves
        if abs(ratio - expect) / expect < TRACK_TOL:
            votes.append("tracks")
        elif abs(ratio - 1.0) < TRACK_TOL:
            votes.append("fixed")
    if not votes:
        return "mixed"
    if all(v == "tracks" for v in votes):
        return "tracks"
    if all(v == "fixed" for v in votes):
        return "fixed"
    return "mixed"


NOTE_ORDER = ["C1", "C2", "C3", "C4", "C5", "C6"]


def classify(rows, note_median):
    """Assign flags to every row of one preset; needs the whole register."""
    motion = peak_motion(rows)
    for r in rows:
        if r.get("err"):
            r["flags"] = ""
            continue
        f = []
        med = note_median.get(r["note"], 0.0)
        r["SHARP_exc"] = round(r["SHARP_acum"] - med, 3)
        if r["SHARP_acum"] >= SHARP_ABS and r["SHARP_exc"] >= SHARP_EXC:
            f.append("SHARP")
        if (r["PROM_db"] >= PROM_GATE and r["REL_db"] >= REL_GATE_FIX
                and motion in ("fixed", "n/a")):
            f.append("FIXPEAK")
        if r["HF8K_pct"] >= HF8K_GATE:
            f.append("HASH")
        r["flags"] = "+".join(f)
        r["peak_motion"] = motion
    return motion


# --------------------------------------------------------------------------
# Modes
# --------------------------------------------------------------------------
def calibrate():
    """Synthetic ground truth: sweep filt_res on a neutral square voice."""
    rows = []
    base_params = {
        "oscA_on": 1, "oscB_on": 0, "sub_on": 0, "noise_on": 0,
        "uni_count": 1, "filt_type": 0, "filt_cutoff": 3000, "filt_env": 0,
        "filt_keytrack": 0, "env1_a": 0.005, "env1_d": 0.1, "env1_s": 0.9,
        "env1_r": 0.1, "env2_pitch": 0, "lfo1_pwm": 0, "lfo2_amt": 0,
        "crush_mix": 0, "dly_mix": 0, "cave_mix": 0, "raw": 0,
        "master_gain": -12,
    }
    variants = [("pw50", {}), ("pw25", {"oscA_pw": 25}), ("pw12", {"oscA_pw": 12}),
                ("uni7", {"uni_count": 7, "uni_detune": 30}),
                ("noise", {"noise_on": 1, "noise_level": 0.6}),
                ("lp12", {"filt_type": 1}), ("bp", {"filt_type": 2}),
                ("cut1500", {"filt_cutoff": 1500}),
                ("cut6000", {"filt_cutoff": 6000}),
                ("raw", {"raw": 1, "filt_cutoff": 12000})]
    for vname, extra in variants:
        for res in [0.10, 0.40, 0.60, 0.70, 0.80, 0.85, 0.90, 0.95, 1.00]:
            pr = dict(base_params)
            pr.update(extra)
            pr["filt_res"] = res
            path = write_temp_preset(f"CAL_{vname}_{int(res*100)}", "LEAD",
                                     "LEAD", [""] * 8, pr)
            for note in ["C2", "C3", "C4", "C5", "C6"]:
                m, err = measure(path, note, "cal")
                if m:
                    rows.append(dict(m, var=vname, cres=res, note=note))
    # keytrack rescue at high res
    for kt in [0.0, 0.3, 0.6, 1.0]:
        pr = dict(base_params)
        pr["filt_res"] = 0.95
        pr["filt_keytrack"] = kt
        path = write_temp_preset(f"CAL_kt{int(kt*100)}", "LEAD", "LEAD", [""] * 8, pr)
        for note in ["C2", "C4", "C6"]:
            m, err = measure(path, note, "cal")
            if m:
                rows.append(dict(m, var=f"kt{kt}", cres=0.95, note=note))
    with open(f"{TMP}/calibration.csv", "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["variant", "filt_res", "note", "PROM_db", "PROM_hz",
                    "REL_db", "HF8K_pct", "PAIN_pct", "win_ms"])
        for r in rows:
            w.writerow([r["var"], r["cres"], r["note"], r["res"], r["res_hz"],
                        r["rel"], r["hf8k"], r["pain"], r["win_ms"]])
    print(json.dumps(rows))
    return rows


FIELDS = ["name", "cat", "note", "SHARP_acum", "SHARP_exc", "PROM_db",
          "PROM_hz", "peak_motion", "REL_db", "HF8K_pct", "PAIN_pct", "K_db",
          "SHARP_noRes", "PAIN_noRes_pct", "RESLIFT_db", "PROM_noRes_db",
          "win_ms", "flags", "err"]


def scan_one(path, name, cat, pitches):
    out = []
    for note in pitches:
        m, err = measure(path, note, "s")
        if m is None:
            out.append(dict(name=name, cat=cat, note=note, err=err, flags=""))
            continue
        vp = variant_path(path, ABL_RES, "nores")
        m0, err0 = measure(vp, note, "s0")
        reslift = lift_db(m["pain"], m0["pain"]) if m0 else 0.0
        row = dict(name=name, cat=cat, note=note, err="",
                   SHARP_acum=m["sharp"], PROM_db=m["res"], PROM_hz=m["res_hz"],
                   REL_db=m["rel"], HF8K_pct=m["hf8k"], PAIN_pct=m["pain"],
                   K_db=m["kdb"], SHARP_noRes=(m0 or {}).get("sharp"),
                   PAIN_noRes_pct=(m0 or {}).get("pain"),
                   RESLIFT_db=reslift,
                   PROM_noRes_db=(m0 or {}).get("res"),
                   win_ms=m["win_ms"])
        out.append(row)
    return out


def scan():
    files = sorted(f for f in os.listdir(FACTORY) if f.endswith(".json"))
    presets = []
    for n, fn in enumerate(files):
        path = f"{FACTORY}/{fn}"
        with open(path) as f:
            js = json.load(f)
        cat = js.get("category", "?")
        name = js.get("name", fn[:-5])
        presets.append((name, cat, scan_one(path, name, cat,
                                            PITCHES.get(cat, PITCHES_DEFAULT))))
        print(f"[{n+1}/{len(files)}] {name}", file=sys.stderr)

    allrows = [r for _, _, rs in presets for r in rs if not r.get("err")]
    note_median = {}
    for note in NOTE_ORDER:
        v = [r["SHARP_acum"] for r in allrows if r["note"] == note]
        if v:
            note_median[note] = statistics.median(v)
    for _, _, rs in presets:
        classify(rs, note_median)

    rows = [r for _, _, rs in presets for r in rs]
    os.makedirs(OUTDIR, exist_ok=True)
    with open(f"{OUTDIR}/RESONANCE_QC.csv", "w", newline="") as f:
        w = csv.DictWriter(f, fieldnames=FIELDS, extrasaction="ignore")
        w.writeheader()
        for r in rows:
            w.writerow(r)
    with open(f"{TMP}/scan.json", "w") as f:
        json.dump(dict(rows=rows, note_median=note_median), f)
    flagged = {}
    for name, cat, rs in presets:
        hits = [r for r in rs if r.get("flags")]
        if hits:
            worst = max(hits, key=lambda r: r["SHARP_acum"])
            flagged[name] = dict(cat=cat, note=worst["note"],
                                 sharp=worst["SHARP_acum"],
                                 exc=worst.get("SHARP_exc"),
                                 prom=worst["PROM_db"], hz=worst["PROM_hz"],
                                 hf8k=worst["HF8K_pct"],
                                 motion=worst.get("peak_motion"),
                                 flags="+".join(sorted({x for r in hits
                                                        for x in r["flags"].split("+")})))
    print(json.dumps(dict(presets=len(files), rows=len(rows),
                          note_median={k: round(v, 2) for k, v in note_median.items()},
                          flagged=len(flagged), detail=flagged), indent=1))
    return rows


def probe(names):
    """Detail + ablation A/B for one or more presets (file stems)."""
    keys = ["filt_type", "filt_cutoff", "filt_res", "filt_env", "filt_keytrack",
            "env2_a", "env2_d", "env2_s", "env2_r", "env2_pitch", "lfo2_dest",
            "lfo2_amt", "lfo2_shape", "lfo2_rate", "uni_count", "uni_detune",
            "oscA_pw", "oscB_pw", "oscB_sync", "raw", "crush_bits",
            "crush_down", "crush_mix", "noise_on", "noise_level", "noise_mode",
            "cave_mix", "dly_mix", "master_gain"]
    for nm in names:
        path = f"{FACTORY}/{nm}.json"
        with open(path) as f:
            js = json.load(f)
        cat = js.get("category", "?")
        p = dump_params(path)
        print(f"=== {nm}  [{cat}]  craft={js.get('craft')}  overrides={js.get('params')}")
        print("    " + "  ".join(f"{k}={p.get(k)}" for k in keys))
        abls = [("shipped", {}), ("res0.12", ABL_RES), ("kt0.6", ABL_KT)]
        if p.get("raw"):
            abls.append(("raw off", ABL_RAW))
        if p.get("crush_mix", 0) > 0.01:
            abls.append(("crush off", ABL_CRUSH))
        for note in PITCHES.get(cat, PITCHES_DEFAULT):
            print(f"  {note}:")
            for tag, ov in abls:
                vp = path if not ov else variant_path(path, ov, "probe")
                m, err = measure(vp, note, "p")
                if m is None:
                    print(f"     {tag:10s} {err}")
                else:
                    print(f"     {tag:10s} SHARP={m['sharp']:5.2f}  "
                          f"PROM={m['res']:6.1f}dB @{m['res_hz']:7.0f}Hz "
                          f"REL={m['rel']:6.1f}  PAIN={m['pain']:6.2f}%  "
                          f"HF8K={m['hf8k']:6.2f}%  K={m['kdb']:6.1f}")


if __name__ == "__main__":
    mode = sys.argv[1] if len(sys.argv) > 1 else "--scan"
    if mode == "--calibrate":
        calibrate()
    elif mode == "--probe":
        probe(sys.argv[2:])
    else:
        scan()
