#!/usr/bin/env python3
"""Summarises the efficiency measurements and prints each point next to the two models."""
import csv
import os
import statistics as st
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SOURCE_FILE = os.path.join(HERE, "..", "Lab1", "penguin.gif")
CSV_PATH = sys.argv[1] if len(sys.argv) > 1 else os.path.join(HERE, "measurements.csv")

DATA = open(SOURCE_FILE, "rb").read()
F = len(DATA)
HEADER, TRAILER = 4, 2  # F A C BCC1 / BCC2 F (BCC2 can be stuffed, handled below)
REPLY = 5
PH = 3


def stuffed(chunk):
    return sum(2 if b in (0x7E, 0x7D) else 1 for b in chunk)


def frames_on_wire(L):
    """Bytes on the line for each data frame at packet size L, with real stuffing (BCC2 included)."""
    chunk = L - PH
    sizes = []
    for i in range(0, F, chunk):
        part = DATA[i:i + chunk]
        size = len(part).to_bytes(2, "big")
        packet = bytes([2]) + size + part
        bcc = 0
        for b in packet:
            bcc ^= b
        sizes.append(HEADER + stuffed(packet) + stuffed([bcc]) + 1)
    return sizes


def control_frames():
    """START and END frames; their packet size comes from the payload total the runs printed (11040 bytes)."""
    each = (11040 - F - PH * len(frames_on_wire(1000))) // 2
    return [HEADER + each + 2] * 2


def actual_delay(tprop, C):
    """The cable rounds the delay to a whole number of byte times, as it prints when configured."""
    byte_ns = int(1e10 / C)
    ns = int(round(tprop * 1e9))
    count = ns // byte_ns + (1 if ns % byte_ns > byte_ns // 2 else 0)
    return count * byte_ns / 1e9


def s_theory(tprop, C, L, fer=0.0):
    tf = 8 * L / C
    return (1 - fer) / (1 + 2 * tprop / tf)


def time_model(C, tprop, L):
    """Transfer time with no errors: every frame plus reply, plus a round trip."""
    frames = frames_on_wire(L) + control_frames()
    delay = actual_delay(tprop, C)
    return sum(10 * (n + REPLY) / C + 2 * delay for n in frames), len(frames)


def s_corrected(C, tprop, L, fer=0.0):
    t, _ = time_model(C, tprop, L)
    return 8 * F / (C * t) * (1 - fer)


def load():
    with open(CSV_PATH) as handle:
        return list(csv.DictReader(handle))


def summarise(rows):
    groups = {}
    for r in rows:
        groups.setdefault((r["sweep"], int(r["point"])), []).append(r)
    out = []
    for (sweep, point), runs in sorted(groups.items()):
        r0 = runs[0]
        C, tp, L, ber = int(r0["C"]), float(r0["tprop_ms"]) / 1000, int(r0["L"]), float(r0["ber"])
        # Time and retransmission counts are valid wherever the transmitter finished the file
        good = [r for r in runs if r["outcome"] in ("complete", "corrupted")]
        times = [float(r["tx_time_s"]) for r in good]
        S = [8 * F / (t * C) for t in times]
        sent = [int(r["frames_sent"]) for r in good]
        retx = [int(r["retransmissions"]) for r in good]
        meas_fer = [x / (x + y) if x + y else 0 for x, y in zip(retx, sent)]
        out.append(dict(
            sweep=sweep, point=point, C=C, tprop=tp, L=L, ber=ber, target=r0["target_fer"],
            n=len(runs), complete=sum(r["outcome"] == "complete" for r in runs),
            corrupted=sum(r["outcome"] == "corrupted" for r in runs),
            failed=sum(r["outcome"] in ("failed", "hung") for r in runs),
            t=st.mean(times) if times else float("nan"),
            S=st.mean(S) if S else float("nan"),
            Ssd=st.stdev(S) if len(S) > 1 else 0.0,
            R=st.mean(8 * F / t for t in times) if times else float("nan"),
            fer=st.mean(meas_fer) if meas_fer else float("nan"),
            fersd=st.stdev(meas_fer) if len(meas_fer) > 1 else 0.0,
            retx=st.mean(retx) if retx else float("nan"),
            to=st.mean(int(r["timeouts"]) for r in good) if good else float("nan"),
            rej=st.mean(int(r["rej"]) for r in good) if good else float("nan"),
            dup=st.mean(int(r["duplicates"] or 0) for r in good) if good else float("nan"),
        ))
    return out


if __name__ == "__main__":
    rows = load()
    print("file bytes", F, "data frames at L=1000", len(frames_on_wire(1000)),
          "wire bytes", sum(frames_on_wire(1000)), "control", control_frames(),
          "stuffed extra bytes", stuffed(DATA) - F)
    print("runs", len(rows), {o: sum(r["outcome"] == o for r in rows)
                              for o in sorted({r["outcome"] for r in rows})})
    print("sweep pt C Tp_ms L BER | n ok bad fail | t R S sd | FERm sd retx to rej dup | Stheory Scorrected")
    for p in summarise(rows):
        a = s_theory(p["tprop"], p["C"], p["L"], p["fer"] if p["ber"] else 0)
        b = s_corrected(p["C"], p["tprop"], p["L"], p["fer"] if p["ber"] else 0)
        print(f"{p['sweep']:7}{p['point']:2} {p['C']:6} {p['tprop']*1000:4.0f} {p['L']:4} {p['ber']:.2e} | "
              f"{p['n']} {p['complete']} {p['corrupted']} {p['failed']} | "
              f"{p['t']:7.2f} {p['R']:7.0f} {p['S']:.3f} {p['Ssd']:.3f} | "
              f"{p['fer']:.3f} {p['fersd']:.3f} {p['retx']:5.1f} {p['to']:4.1f} {p['rej']:4.1f} {p['dup']:4.1f} | "
              f"{a:.3f} {b:.3f}")
