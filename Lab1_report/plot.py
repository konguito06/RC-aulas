#!/usr/bin/env python3
"""Draws the report's 2x2 efficiency figure and prints the numbers used in its tables."""
import os
import statistics as st

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

import analyse as A

HERE = os.path.dirname(os.path.abspath(__file__))
INK, MUTED, GRID, SHADE = "#0b0b0b", "#52514e", "#e4e3df", "#f1f0ec"
# Colour names the test condition; line style names the model (solid corrected, dashed theoretical)
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


def models(ax, xs, corrected, theory, color):
    ax.plot(xs, theory, color=color, lw=1.3, ls=(0, (4, 2.5)), alpha=0.75, zorder=2)
    ax.plot(xs, corrected, color=color, lw=1.8, zorder=2)


def measured(ax, pts, x, color, marker="o", xerr=None):
    ax.errorbar(x, [p["S"] for p in pts], yerr=[p["Ssd"] for p in pts], xerr=xerr, fmt=marker, ms=6.5,
                color=color, mec="white", mew=0.9, ecolor=color, elinewidth=0.9, capsize=2.5, zorder=3)


fig, axes = plt.subplots(2, 2, figsize=(6.7, 5.1))
(a, b), (c, d) = axes

pts = sweep("C")
xs = [1000, 130000]
a.axvspan(1000, 2562, color=SHADE, zorder=0)
a.text(1070, 0.05, "frame cycle\n> 4 s timer", fontsize=8.5, color=MUTED, va="bottom")
models(a, xs, [A.s_corrected(9600, 0, 1000)] * 2, [1, 1], BLUE)
measured(a, pts, [p["C"] for p in pts], BLUE)
a.annotate("each frame sent twice", xy=(2400, 0.393), xytext=(4300, 0.27), fontsize=8.5, color=INK,
           arrowprops=dict(arrowstyle="-", color=MUTED, lw=0.6))
a.set_xscale("log")
a.set_xlim(1000, 130000)
a.set_xticks([1200, 2400, 4800, 9600, 19200, 38400, 115200])
a.set_xticklabels(["1.2k", "2.4k", "4.8k", "9.6k", "19.2k", "38.4k", "115.2k"])
a.minorticks_off()
a.set(title="(a) Baud rate", xlabel="C (bit/s)", ylabel="Efficiency S")

pts = sweep("Tprop")
curve = [i / 1000 for i in range(0, 501, 5)]
models(b, [t * 1000 for t in curve], [A.s_corrected(9600, t, 1000) for t in curve],
       [A.s_theory(t, 9600, 1000) for t in curve], BLUE)
measured(b, pts, [p["tprop"] * 1000 for p in pts], BLUE)
b.set(title="(b) Propagation delay (C = 9600 bit/s)", xlabel="T$_{prop}$ (ms)", ylabel="Efficiency S")

curve = [i / 100 for i in range(0, 61)]
s0 = A.s_corrected(9600, 0, 1000)
# Both series share the same model curves, so they are drawn in ink
models(c, curve, [s0 * (1 - f) for f in curve], [1 - f for f in curve], INK)
for name, color, marker in (("FER", BLUE, "o"), ("FER_INJ", ORANGE, "s")):
    pts = sweep(name)
    measured(c, pts, [p["fer"] for p in pts], color, marker, xerr=[p["fersd"] for p in pts])
c.legend(handles=[
    Line2D([], [], color=BLUE, marker="o", ms=5.5, mec="white", ls="none", label="Cable noise (15 runs)"),
    Line2D([], [], color=ORANGE, marker="s", ms=5.5, mec="white", ls="none", label="Injected REJ (5 runs)"),
], loc="lower left")
c.set(title="(c) Frame error rate (C = 9600 bit/s, L = 1000)", xlabel="Measured FER", ylabel="Efficiency S")

curve = [125, 150, 200, 250, 300, 400, 500, 600, 750, 1000]
ber = 1 - (1 - 0.2) ** (1 / 8000)
models(d, curve, [A.s_corrected(9600, 0, L) for L in curve], [1] * len(curve), BLUE)
models(d, curve, [A.s_corrected(9600, 0.1, L) for L in curve], [A.s_theory(0.1, 9600, L) for L in curve], ORANGE)
fer = lambda L: 1 - (1 - ber) ** (8 * L)
models(d, curve, [A.s_corrected(9600, 0, L, fer(L)) for L in curve], [1 - fer(L) for L in curve], AQUA)
# The BER points are shifted 6% right so they do not hide the other series at the same L
for name, color, marker, shift in (("L_T0", BLUE, "o", 1), ("L_T100", ORANGE, "s", 1), ("L_BER", AQUA, "^", 1.06)):
    pts = sweep(name)
    measured(d, pts, [p["L"] * shift for p in pts], color, marker)
d.set_xscale("log")
d.set_xticks([125, 250, 500, 1000])
d.set_xticklabels(["125", "250", "500", "1000"])
d.minorticks_off()
d.set(title="(d) Frame size (C = 9600 bit/s)", xlabel="L (bytes)", ylabel="Efficiency S")
d.legend(handles=[
    Line2D([], [], color=BLUE, marker="o", ms=5.5, mec="white", lw=1.8, label="T$_{prop}$ = 0, BER = 0"),
    Line2D([], [], color=ORANGE, marker="s", ms=5.5, mec="white", lw=1.8, label="T$_{prop}$ = 100 ms"),
    Line2D([], [], color=AQUA, marker="^", ms=5.5, mec="white", lw=1.8, label="BER = 2.8·10$^{-5}$"),
], loc="lower right", handlelength=2.2)

for ax in axes.flat:
    ax.set_ylim(0, 1.06)
    ax.set_yticks([0, 0.2, 0.4, 0.6, 0.8, 1.0])

key = [
    Line2D([], [], color=INK, marker="o", ms=6, mec="white", ls="none", label="Measured (mean ± SD)"),
    Line2D([], [], color=INK, lw=1.8, label="Corrected model (Eq. 2)"),
    Line2D([], [], color=INK, lw=1.3, ls=(0, (4, 2.5)), alpha=0.75, label="Theoretical formula (Eq. 1)"),
]
fig.legend(handles=key, loc="upper center", ncol=3, bbox_to_anchor=(0.5, 1.0), handlelength=2.6, columnspacing=2.2,
           fontsize=9)
fig.tight_layout(pad=0.4, h_pad=0.9, w_pad=1.1, rect=(0, 0, 1, 0.94))
fig.savefig(os.path.join(HERE, "efficiency.svg"))

print("sweep | mean abs diff theory | corrected | max corrected")
for name in ("C", "Tprop", "L_T0", "L_T100", "FER", "FER_INJ", "L_BER"):
    pts = [p for p in sweep(name) if not (name == "C" and p["C"] <= 2400)]
    measured_fer = lambda p: p["fer"] if p["sweep"] in ("FER", "FER_INJ", "L_BER") else 0
    e1 = [abs(p["S"] - A.s_theory(p["tprop"], p["C"], p["L"], measured_fer(p))) for p in pts]
    e2 = [abs(p["S"] - A.s_corrected(p["C"], p["tprop"], p["L"], measured_fer(p))) for p in pts]
    print(f"{name:7} {st.mean(e1):.3f} {st.mean(e2):.4f} {max(e2):.4f}")
