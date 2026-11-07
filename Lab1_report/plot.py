#!/usr/bin/env python3
"""Draws the report's 2x2 efficiency figure and prints the numbers used in its tables."""
import os
import statistics as st

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt

import analyse as A

HERE = os.path.dirname(os.path.abspath(__file__))
INK, MUTED, GRID = "#0b0b0b", "#52514e", "#e4e3df"
BLUE, ORANGE, AQUA = "#2a78d6", "#eb6834", "#1baf7a"

plt.rcParams.update({
    "font.family": "serif", "font.serif": ["Times New Roman", "DejaVu Serif"], "font.size": 9.5,
    "axes.edgecolor": MUTED, "axes.linewidth": 0.6, "axes.titlesize": 10, "axes.titleweight": "bold",
    "axes.labelcolor": INK, "xtick.color": MUTED, "ytick.color": MUTED, "svg.fonttype": "path",
    "axes.grid": True, "grid.color": GRID, "grid.linewidth": 0.5, "axes.axisbelow": True,
    "axes.spines.top": False, "axes.spines.right": False, "legend.frameon": False, "legend.fontsize": 8.5,
})

rows = A.summarise(A.load())
sweep = lambda name: [p for p in rows if p["sweep"] == name]
THEORY = dict(color=MUTED, lw=1.1, ls=(0, (4, 2)), label="Theoretical formula")
CORR = dict(color=INK, lw=1.1, label="Corrected model")
MEAS = dict(fmt="o", ms=4.5, capsize=2, elinewidth=0.8, mec="white", mew=0.5)

fig, axes = plt.subplots(2, 2, figsize=(6.7, 4.75))
(a, b), (c, d) = axes

pts = sweep("C")
xs = [p["C"] for p in pts]
a.plot(xs, [1] * len(xs), **THEORY)
a.plot(xs, [A.s_corrected(x, 0, 1000) for x in xs], **CORR)
a.errorbar(xs, [p["S"] for p in pts], yerr=[p["Ssd"] for p in pts], color=BLUE, label="Measured", **MEAS)
a.set_xscale("log")
a.set_xticks([1200, 2400, 4800, 9600, 19200, 38400, 115200])
a.set_xticklabels(["1.2k", "2.4k", "4.8k", "9.6k", "19.2k", "38.4k", "115.2k"])
a.minorticks_off()
a.annotate("frame cycle > 4 s timer:\neach frame sent twice", xy=(2400, 0.393), xytext=(4300, 0.2),
           fontsize=8.5, color=INK, arrowprops=dict(arrowstyle="-", color=MUTED, lw=0.6))
a.set(title="(a) Baud rate", xlabel="C (bit/s)", ylabel="Efficiency S")
a.legend(loc="center right", bbox_to_anchor=(1, 0.52))

pts = sweep("Tprop")
curve = [i / 1000 for i in range(0, 501, 5)]
b.plot([t * 1000 for t in curve], [A.s_theory(t, 9600, 1000) for t in curve], **THEORY)
b.plot([t * 1000 for t in curve], [A.s_corrected(9600, t, 1000) for t in curve], **CORR)
b.errorbar([p["tprop"] * 1000 for p in pts], [p["S"] for p in pts], yerr=[p["Ssd"] for p in pts],
           color=BLUE, label="Measured", **MEAS)
b.set(title="(b) Propagation delay (C = 9600 bit/s)", xlabel="T$_{prop}$ (ms)", ylabel="Efficiency S")
b.legend(loc="upper right")

pts = sweep("FER")
curve = [i / 100 for i in range(0, 61)]
s0 = A.s_corrected(9600, 0, 1000)
c.plot(curve, [1 - f for f in curve], **THEORY)
c.plot(curve, [s0 * (1 - f) for f in curve], **CORR)
c.errorbar([p["fer"] for p in pts], [p["S"] for p in pts], xerr=[p["fersd"] for p in pts],
           yerr=[p["Ssd"] for p in pts], color=BLUE, label="Measured (mean ± SD)", **MEAS)
c.set(title="(c) Frame error rate (C = 9600 bit/s, L = 1000)", xlabel="Measured FER", ylabel="Efficiency S")
c.legend(loc="upper right")

curve = [125, 150, 200, 250, 300, 400, 500, 600, 750, 1000]
d.plot(curve, [A.s_theory(0.1, 9600, L) for L in curve], color=MUTED, lw=1.1, ls=(0, (4, 2)), label="Theoretical")
d.plot(curve, [1] * len(curve), color=MUTED, lw=1.1, ls=(0, (4, 2)))
d.plot(curve, [A.s_corrected(9600, 0, L) for L in curve], **{**CORR, "label": "Corrected"})
d.plot(curve, [A.s_corrected(9600, 0.1, L) for L in curve], color=INK, lw=1.1)
for name, color, marker, label in (("L_T0", BLUE, "o", "T$_{prop}$ = 0"), ("L_BER", AQUA, "^", "BER = 2.8·10$^{-5}$"),
                                   ("L_T100", ORANGE, "s", "T$_{prop}$ = 100 ms")):
    pts = sweep(name)
    d.errorbar([p["L"] for p in pts], [p["S"] for p in pts], yerr=[p["Ssd"] for p in pts], color=color,
               label=label, **{**MEAS, "fmt": marker})
d.set_xscale("log")
d.set_xticks([125, 250, 500, 1000])
d.set_xticklabels(["125", "250", "500", "1000"])
d.minorticks_off()
d.set(title="(d) Frame size (C = 9600 bit/s)", xlabel="L (bytes)", ylabel="Efficiency S")
d.legend(loc="lower right", ncol=2, columnspacing=0.8, handlelength=1.6)

for ax in axes.flat:
    ax.set_ylim(0, 1.06)
    ax.set_yticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])
fig.tight_layout(pad=0.4, h_pad=0.9, w_pad=1.1)
fig.savefig(os.path.join(HERE, "efficiency.svg"))

print("sweep | mean abs diff slide | corrected | max corrected")
for name in ("C", "Tprop", "L_T0", "L_T100", "FER", "L_BER"):
    pts = [p for p in sweep(name) if not (name == "C" and p["C"] <= 2400)]
    fer = lambda p: p["fer"] if p["ber"] else 0
    e1 = [abs(p["S"] - A.s_theory(p["tprop"], p["C"], p["L"], fer(p))) for p in pts]
    e2 = [abs(p["S"] - A.s_corrected(p["C"], p["tprop"], p["L"], fer(p))) for p in pts]
    print(f"{name:7} {st.mean(e1):.3f} {st.mean(e2):.4f} {max(e2):.4f}")
print("ratio slide/measured:", [round(A.s_theory(p['tprop'], 9600, 1000) / p['S'], 3) for p in sweep('Tprop')])
print("L_BER FER:", [(p['L'], round(p['fer'], 3), round(p['S'], 3), round(p['Ssd'], 3)) for p in sweep('L_BER')])
