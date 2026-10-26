"""All pH buffer readings from 2026-10-25, in chronological order. Writes gnuplot data + stats.

Sources: ph686-capture-0447.log and setcal-save.log (saved logs); other sessions were
pasted into the chat from the user's serial monitor and transcribed here verbatim.
Run: python sessions.py  (then gnuplot honest.gp)
"""
import itertools
import statistics as st

S = []  # (name, buffer pH, kind, values, settled-from index, note)
S.append(("S1 6.86 (04:46 log)", 6.86, "neutral", [float(v) for v in """
1749.438 1749.500 1749.438 1749.438 1749.500 1749.625 1749.625 1749.531 1749.625 1749.531 1749.625 1749.562 1749.625 1749.594 1749.656 1749.719 1749.781 1749.688 1749.875 1749.781 1749.812 1749.719 1749.969 1750.000 1750.031 1749.938 1749.938 1750.000 1749.969 1750.156 1750.250 1750.188 1750.312 1750.312 1750.344 1750.344 1750.500 1750.219 1750.438 1750.438 1750.562 1750.562""".split()], 0, ""))
S.append(("S2 pH 4 #1 (paste)", 4.00, "acid", [float(v) for v in """
1651.688 1651.594 1651.594 1651.562 1651.406 1651.406 1651.312 1651.344 1651.125 1651.312 1651.094 1651.281 1650.875 1650.938 1650.656 1650.750 1651.062 1650.719 1650.656 1650.469 1650.625 1650.469 1650.406 1650.281 1650.250 1650.312 1650.281 1650.219 1650.156 1650.125 1650.125 1650.031 1650.031 1650.000 1650.031 1649.906 1649.938 1649.781 1649.688 1649.750 1649.656 1649.656 1649.531 1649.469 1649.406 1649.406 1649.406 1649.438 1649.188 1649.250 1649.219 1649.281 1649.344 1649.156 1649.031 1649.219 1649.062 1649.188 1649.156 1649.062 1649.188 1649.000 1648.781 1648.781 1648.906 1648.875 1648.781 1648.594 1648.750 1648.594 1648.719 1648.531 1648.688 1648.531 1648.594 1648.594 1648.531 1648.562 1648.531 1648.656 1648.531 1648.719 1648.562 1648.656 1648.438 1648.594 1648.500 1648.531 1648.500 1648.562 1648.438 1648.531 1648.406 1648.438 1648.438 1648.438 1648.344 1648.531 1648.469 1648.281 1648.312 1648.250 1648.344 1648.469 1648.250 1648.250 1648.188 1648.469 1648.312 1648.375 1648.438 1648.406 1648.375 1648.281 1648.406""".split()], 0, ""))
S.append(("S3 pH 4 #2 (paste)", 4.00, "acid", [float(v) for v in """
1670.156 1670.031 1670.188 1669.969 1669.938 1669.969 1669.812 1669.906 1669.844 1669.906 1669.875 1669.969 1669.688 1669.812 1669.812 1669.625 1669.500 1669.688 1669.406 1669.281 1669.344 1669.219 1669.156 1669.188 1669.062 1669.188 1669.156 1669.062 1668.906 1668.938 1668.969 1669.000 1669.000 1668.875 1668.844 1669.000 1668.844 1668.844 1669.000 1668.750 1668.750 1668.750 1668.688 1668.781 1668.531 1668.719 1668.656 1668.438 1668.469 1668.562 1668.531 1668.375 1668.344 1668.438 1668.312 1668.406 1668.344 1668.281 1668.219 1668.312 1668.188 1668.188 1668.219 1668.062 1668.281 1668.062 1668.250 1667.969 1668.031 1668.000 1668.156 1668.125 1668.062 1667.719 1668.031 1667.844 1668.000 1667.844 1667.938 1667.906 1667.875 1667.781 1667.906 1667.781 1667.750 1667.781 1667.625 1667.719 1667.750 1667.719 1667.656 1667.656 1667.719 1667.438 1667.750 1667.625 1667.719 1667.625""".split()], 0, ""))
S.append(("S4 6.85 (paste)", 6.85, "neutral", [float(v) for v in """
1742.500 1742.312 1742.438 1742.375 1742.312 1742.375 1742.500 1742.406 1742.375 1742.688 1742.438 1742.500 1742.594 1742.719 1742.562""".split()], 0, ""))
S.append(("S5 pH 4 #3 (05:20 log)", 4.00, "acid", [float(v) for v in """
1667.438 1667.344 1667.625 1667.438 1667.625 1667.781 1667.656 1667.750 1667.531 1667.625 1667.688 1667.594 1667.656 1667.500 1667.500 1667.500 1667.688""".split()], 0, ""))
D = [float(v) for v in open("settling-mv.dat").read().split("\n", 1)[1].split()]
S.append(("S6a pH 4 #4 (paste)", 4.00, "acid", D[:8], 0, ""))
S.append(("S6b moved to 6.85 (paste)", 6.85, "neutral", D[8:], 0, "still rising"))

def settled(vals):
    tail = vals[-10:]
    return st.mean(tail), min(vals[-30:]), max(vals[-30:])

# Readings only known as summaries (no full log kept).
extra = [("S0 'ph7 or 6.85' (04:43 summary, n=31)", 6.85, "neutral", 1744.886, 1744.469, 1745.312)]

with open("all-readings.dat", "w") as f:
    f.write("# seq mV kind(0 neutral,1 acid)\n")
    seq = 0
    for name, ph, kind, vals, _, _ in S:
        f.write(f"# {name}\n")
        for v in vals:
            f.write(f"{seq} {v:.3f}\n")
            seq += 1
        f.write("\n\n")
        seq += 15
with open("session-labels.dat", "w") as f:
    seq = 0
    for i, (name, ph, kind, vals, _, _) in enumerate(S):
        f.write(f'{seq + len(vals) / 2:.1f} "{name.split(" (")[0]}"\n')
        seq += len(vals) + 15

pts = []
with open("settled.dat", "w") as f:
    f.write("# mV_mean mV_min mV_max true_pH kind label\n")
    for name, ph, kind, vals, _, note in S:
        m, lo, hi = settled(vals)
        f.write(f'{m:.3f} {lo:.3f} {hi:.3f} {ph} {0 if kind == "neutral" else 1} "{name.split(" (")[0]}{" *" if note else ""}"\n')
        pts.append((name, ph, kind, m))
    for name, ph, kind, m, lo, hi in extra:
        f.write(f'{m:.3f} {lo:.3f} {hi:.3f} {ph} 0 "S0"\n')
        pts.append((name, ph, kind, m))

neutral = [p for p in pts if p[2] == "neutral"]
acid = [p for p in pts if p[2] == "acid"]
probe = 1700.0
results = []
with open("fan.dat", "w") as f:
    for n, a in itertools.product(neutral, acid):
        f.write(f"{n[3]:.3f} {n[1]} {a[3]:.3f} {a[1]}\n")
        ph = n[1] + (probe - n[3]) * (a[1] - n[1]) / (a[3] - n[3])
        results.append((ph, n[0], a[0], (n[3] - a[3]) / (n[1] - a[1])))
nm, am = st.mean(p[3] for p in neutral), st.mean(p[3] for p in acid)
nph, aph = st.mean(p[1] for p in neutral), 4.0
with open("pooled.dat", "w") as f:
    f.write(f"{nm:.3f} {nph:.4f} {am:.3f} {aph}\n")
phs = [r[0] for r in results]
print(f"settled neutral mV: {[round(p[3], 2) for p in neutral]}")
print(f"settled acid mV: {[round(p[3], 2) for p in acid]}")
print(f"pooled: pH {nph:.3f} at {nm:.2f} mV, pH 4.00 at {am:.2f} mV, {(nm - am) / (nph - 4):.1f} mV/pH")
print(f"{len(results)} possible calibrations; at {probe} mV they give pH {min(phs):.2f} .. {max(phs):.2f}")
print(f"slopes mV/pH: {min(r[3] for r in results):.1f} .. {max(r[3] for r in results):.1f}")
