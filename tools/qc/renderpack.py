#!/usr/bin/env python3
"""BLOCKWAVE Phase 6 render pack: batch render + auto-QC + normalize.
Stdlib only. LUFS proxy = plain RMS dBFS (documented). Dup detection = coarse
time-domain feature cosine (flag only, never auto-reject).
"""
import json, os, struct, subprocess, sys, math, csv

ROOT = "/Users/kirillboyko/Downloads/blockwave-kickoff"
RENDER = f"{ROOT}/build/release/render_artefacts/Release/render"
CAND = f"{ROOT}/presets/candidates"
PACK = f"{ROOT}/CHECKPOINTS/artifacts/renderpack"
TMP = os.path.dirname(os.path.abspath(__file__)) + "/rp_tmp"
os.makedirs(PACK, exist_ok=True)
os.makedirs(TMP, exist_ok=True)

NOTE = {"BASS": "C2", "PERC": "C3", "PAD": "C3", "FX": "C3"}  # default C4

# --- minimal format-0 MIDI writer -------------------------------------------
def vlq(n):
    out = [n & 0x7F]; n >>= 7
    while n: out.append(0x80 | (n & 0x7F)); n >>= 7
    return bytes(reversed(out))

def midi_file(events, path):
    """events: list of (delta_ticks, status, d1, d2); 480 ticks/quarter, 120bpm."""
    track = b"\x00\xff\x51\x03" + (500000).to_bytes(3, "big")  # tempo 120
    for delta, st, d1, d2 in events:
        track += vlq(delta) + bytes([st, d1, d2])
    track += b"\x00\xff\x2f\x00"
    with open(path, "wb") as f:
        f.write(b"MThd" + struct.pack(">IHHH", 6, 0, 1, 480))
        f.write(b"MTrk" + struct.pack(">I", len(track)) + track)

Q = 480  # quarter note
def seq(notes):  # [(note, start_q, len_q, vel)]
    ev = []
    for n, s, l, v in notes:
        ev.append((int(s * Q), True, n, v)); ev.append((int((s + l) * Q), False, n, 0))
    ev.sort(key=lambda e: (e[0], e[1]))  # offs before ons at same tick
    out, last = [], 0
    for t, on, n, v in ev:
        out.append((t - last, 0x90 if on else 0x80, n, v)); last = t
    return out

def phrase_for(cat):
    C2, C3, C4 = 36, 48, 60
    if cat in ("PAD", "KEYS"):   # held chord — the distortion-class check
        return seq([(C3, 0, 4, 100), (C3+4, 0, 4, 96), (C3+7, 0, 4, 96), (C3+11, 0, 4, 90)])
    if cat == "LEAD":            # melody + a final dyad
        m = [(C4, 0, .5), (C4+3, .5, .5), (C4+7, 1, .5), (C4+10, 1.5, .5),
             (C4+7, 2, 1)]
        return seq([(n, s, l, 100) for n, s, l in m] + [(C4, 2, 1, 92), (C4+7, 3, 1, 92)])
    if cat == "BASS":
        m = [(C2, 0, .75), (C2, 1, .45), (C2+3, 1.5, .45), (C2, 2, .75), (C2+5, 3, .9)]
        return seq([(n, s, l, 110) for n, s, l in m])
    if cat == "PERC":
        return seq([(C3, i * .5, .4, 115 if i % 2 == 0 else 88) for i in range(8)])
    if cat == "CHIP":
        m = [(C4 + [0, 4, 7, 12][i % 4], i * .25, .22) for i in range(12)]
        return seq([(n, s, l, 104) for n, s, l in m])
    if cat == "PLUCK":
        m = [(C4 + [0, 7, 4, 12, 7, 0][i], i * .4, .3) for i in range(6)]
        return seq([(n, s, l, 102) for n, s, l in m])
    return seq([(C3, 0, 4, 100)])  # FX: long note

PHRASE_MIDI = {}
for cat in ["LEAD", "BASS", "PLUCK", "PAD", "KEYS", "CHIP", "PERC", "FX"]:
    p = f"{TMP}/{cat}.mid"; midi_file(phrase_for(cat), p); PHRASE_MIDI[cat] = p

# --- WAV IO (RIFF, handles float32 fmt 3 and PCM16) --------------------------
def read_wav(path):
    with open(path, "rb") as f: b = f.read()
    assert b[:4] == b"RIFF" and b[8:12] == b"WAVE", path
    pos, fmt, data = 12, None, None
    while pos + 8 <= len(b):
        cid, sz = b[pos:pos+4], struct.unpack("<I", b[pos+4:pos+8])[0]
        body = b[pos+8:pos+8+sz]
        if cid == b"fmt ": fmt = body
        elif cid == b"data": data = body
        pos += 8 + sz + (sz & 1)
    tag, ch, sr = struct.unpack("<HHI", fmt[:8])
    bits = struct.unpack("<H", fmt[14:16])[0]
    if tag == 3 and bits == 32:
        smp = struct.unpack(f"<{len(data)//4}f", data)
    elif tag == 1 and bits == 16:
        smp = [s / 32768.0 for s in struct.unpack(f"<{len(data)//2}h", data)]
    else:
        raise ValueError(f"unsupported wav fmt {tag}/{bits}")
    # deinterleave to mono-ish analysis (mid) but keep stereo for writing
    return sr, ch, smp

def write_wav16(path, sr, ch, smp):
    pcm = struct.pack(f"<{len(smp)}h",
                      *[max(-32768, min(32767, int(round(s * 32767)))) for s in smp])
    hdr = b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVE"
    hdr += b"fmt " + struct.pack("<IHHIIHH", 16, 1, ch, sr, sr * ch * 2, ch * 2, 16)
    hdr += b"data" + struct.pack("<I", len(pcm))
    with open(path, "wb") as f: f.write(hdr + pcm)

def metrics(smp, ch):
    n = len(smp)
    if n == 0: return None
    mean = sum(smp) / n
    rms = math.sqrt(sum(s * s for s in smp) / n)
    peak = max(abs(s) for s in smp)
    hot = sum(1 for s in smp if abs(s) >= 0.99) / n
    # coarse features for dup flagging: rms, zcr, attack proxy, tail proxy
    zc = sum(1 for i in range(ch, n, ch) if (smp[i] >= 0) != (smp[i - ch] >= 0)) / n
    q = n // 4
    head = math.sqrt(sum(s * s for s in smp[:q]) / q)
    tail = math.sqrt(sum(s * s for s in smp[-q:]) / q)
    return dict(mean=mean, rms=rms, peak=peak, hot=hot,
                feat=(rms, zc * 10, head, tail))

def cos(a, b):
    num = sum(x * y for x, y in zip(a, b))
    da = math.sqrt(sum(x * x for x in a)); db = math.sqrt(sum(x * x for x in b))
    return num / (da * db) if da > 1e-12 and db > 1e-12 else 0.0

def db(x): return -120.0 if x <= 1e-6 else 20 * math.log10(x)

# --- main loop ---------------------------------------------------------------
files = sorted(f for f in os.listdir(CAND) if f.endswith(".json"))
rows, rejects, sneaky = [], [], []
by_cat = {}
for fn in files:
    stem = fn[:-5]; cat = stem.split("_")[0]
    path = f"{CAND}/{fn}"
    note = NOTE.get(cat, "C4")
    note_wav = f"{TMP}/{stem}.wav"; phr_wav = f"{TMP}/{stem}_p.wav"
    r1 = subprocess.run([RENDER, path, f"note:{note}:2s", note_wav, "--sr", "48000",
                         "--bpm", "120"], capture_output=True, text=True)
    r2 = subprocess.run([RENDER, path, PHRASE_MIDI[cat], phr_wav, "--sr", "48000",
                         "--bpm", "120"], capture_output=True, text=True)
    if r1.returncode or r2.returncode or not os.path.exists(note_wav) or not os.path.exists(phr_wav):
        rejects.append((stem, f"RENDER FAIL rc={r1.returncode}/{r2.returncode} "
                              f"{(r1.stderr or r2.stderr)[:120]}"))
        continue
    sr, ch, s1 = read_wav(note_wav); _, _, s2 = read_wav(phr_wav)
    m1, m2 = metrics(s1, ch), metrics(s2, ch)
    bad = any(math.isnan(x) or math.isinf(x) for x in (m1["rms"], m2["rms"]))
    verdict, reason = "OK", ""
    if bad: verdict, reason = "REJECT", "NaN/Inf"
    elif db(m1["rms"]) < -50 and db(m2["rms"]) < -50: verdict, reason = "REJECT", f"silent {db(m1['rms']):.1f} dBFS"
    elif m1["hot"] > 0.005 or m2["hot"] > 0.005: verdict, reason = "REJECT", f"softclip-riding hot={max(m1['hot'],m2['hot'])*100:.2f}%"
    elif abs(m1["mean"]) > 0.01: verdict, reason = "REJECT", f"DC {m1['mean']:.4f}"
    rows.append(dict(cat=cat, name=stem, rms_db=round(db(m1["rms"]), 1),
                     peak=round(m1["peak"], 3), dc=round(m1["mean"], 5),
                     hot_pct=round(max(m1["hot"], m2["hot"]) * 100, 3),
                     verdict=verdict, reason=reason, feat=m1["feat"]))
    if verdict == "REJECT": rejects.append((stem, reason))
    else: by_cat.setdefault(cat, []).append(rows[-1])

# duplicate flagging within category (flag worse = quieter one)
for cat, lst in by_cat.items():
    for i in range(len(lst)):
        for j in range(i + 1, len(lst)):
            if cos(lst[i]["feat"], lst[j]["feat"]) > 0.9995 and \
               abs(lst[i]["rms_db"] - lst[j]["rms_db"]) < 1.0:
                worse = lst[i] if lst[i]["rms_db"] < lst[j]["rms_db"] else lst[j]
                if "DUP?" not in worse["reason"]:
                    other = lst[j] if worse is lst[i] else lst[i]
                    worse["reason"] = f"DUP? ~ {other['name']}"

# normalize survivors to -18 dBFS RMS, write pack
count = 0
for row in rows:
    if row["verdict"] != "OK": continue
    stem = row["name"]
    for suffix, src in (("", f"{TMP}/{stem}.wav"), ("_phrase", f"{TMP}/{stem}_p.wav")):
        sr, ch, smp = read_wav(src)
        m = metrics(smp, ch)
        g = (10 ** (-18 / 20)) / m["rms"] if m["rms"] > 1e-6 else 1.0
        pk = m["peak"] * g
        if pk > 0.99: g *= 0.99 / pk
        write_wav16(f"{PACK}/{stem}{suffix}.wav", sr, ch, [s * g for s in smp])
    count += 1

with open(f"{PACK}/_QC_REPORT.csv", "w", newline="") as f:
    w = csv.writer(f)
    w.writerow(["category", "name", "rms_dbfs", "peak", "dc", "hot_pct", "verdict", "note"])
    for r in rows:
        w.writerow([r["cat"], r["name"], r["rms_db"], r["peak"], r["dc"],
                    r["hot_pct"], r["verdict"], r["reason"]])
with open(f"{PACK}/_REJECTED.txt", "w") as f:
    for name, why in rejects: f.write(f"{name}: {why}\n")

sur_by_cat = {c: len(v) for c, v in sorted(by_cat.items())}
dups = [r["name"] + " " + r["reason"] for r in rows if "DUP?" in r["reason"]]
print(json.dumps(dict(total=len(files), rendered=len(rows), rejected=len(rejects),
                      survivors=count, per_cat=sur_by_cat, dup_flags=dups,
                      reject_list=[f"{n}: {w}" for n, w in rejects]), indent=1))
