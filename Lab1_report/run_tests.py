#!/usr/bin/env python3
"""Runs the Lab 1 efficiency sweeps over the virtual cable and writes one CSV row per transfer."""
import csv
import filecmp
import os
import re
import signal
import subprocess
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
BUILD = os.path.join(HERE, "build")
WORK = os.path.join(HERE, "work")
LOGS = os.path.join(WORK, "logs")
SOURCE_FILE = os.path.join(HERE, "..", "Lab1", "penguin.gif")
TX_PORT, RX_PORT = "/tmp/ttyS10", "/tmp/ttyS11"

FIELDS = ["sweep", "point", "run", "C", "tprop_ms", "ber", "L", "target_fer", "outcome", "tx_time_s",
          "frames_sent", "retransmissions", "timeouts", "rej", "duplicates", "rej_sent",
          "file_ok", "close_ok", "wall_s"]


def ber_for_fer(fer, L):
    return 1 - (1 - fer) ** (1 / (8 * L))


def build_plan():
    plan = []
    base = dict(C=9600, tprop_ms=0, ber=0.0, L=1000, target_fer="")
    for c in [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]:
        plan.append(("C", {**base, "C": c}, 3))
    for t in [0, 10, 50, 100, 200, 500]:
        plan.append(("Tprop", {**base, "tprop_ms": t}, 3))
    for fer in [0, 0.1, 0.2, 0.3, 0.4, 0.5]:
        plan.append(("FER", {**base, "ber": ber_for_fer(fer, 1000), "target_fer": fer}, 15))
    # Errors injected at the receiver on I frames (REJ only), as the course guidelines suggest
    for fer in [0, 0.1, 0.2, 0.3, 0.4, 0.5]:
        plan.append(("FER_INJ", {**base, "target_fer": fer, "inject": fer}, 5))
    for L in [125, 250, 500, 1000]:
        plan.append(("L_T0", {**base, "L": L}, 3))
    for L in [125, 250, 500, 1000]:
        plan.append(("L_T100", {**base, "L": L, "tprop_ms": 100}, 3))
    # BER giving FER = 0.2 at L = 1000: shows the trade-off between overhead and frame errors
    for L in [125, 250, 500, 1000]:
        plan.append(("L_BER", {**base, "L": L, "ber": ber_for_fer(0.2, 1000)}, 3))
    return plan


class Cable:
    def __init__(self, tag):
        self.log = open(os.path.join(LOGS, f"cable_{tag}.log"), "w")
        self.proc = subprocess.Popen([os.path.join(BUILD, "cable")], stdin=subprocess.PIPE,
                                     stdout=self.log, stderr=subprocess.STDOUT, cwd=WORK)
        deadline = time.time() + 10
        while not (os.path.exists(TX_PORT) and os.path.exists(RX_PORT)):
            if time.time() > deadline:
                raise RuntimeError("cable did not create the virtual ports")
            time.sleep(0.1)
        time.sleep(1)

    def send(self, command):
        # The cable treats each read as one command, so leave a gap between them
        self.proc.stdin.write((command + "\n").encode())
        self.proc.stdin.flush()
        time.sleep(0.5)

    def configure(self, cfg):
        self.send(f"baud {cfg['C']}")
        self.send(f"prop {int(cfg['tprop_ms'] * 1000)}")
        self.send(f"ber {cfg['ber']:.10f}")

    def quit(self):
        try:
            self.send("quit")
            self.proc.wait(timeout=10)
        except Exception:
            self.proc.kill()
        self.log.close()
        subprocess.run(["pkill", "-x", "socat"], check=False)
        for port in (TX_PORT, RX_PORT):
            deadline = time.time() + 5
            while os.path.exists(port) and time.time() < deadline:
                time.sleep(0.1)


def number(pattern, text, kind=int):
    match = re.search(pattern, text)
    return kind(match.group(1)) if match else ""


def stop(proc):
    if proc.poll() is None:
        proc.send_signal(signal.SIGTERM)
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait()


def one_run(sweep, point, run, cfg):
    binary = os.path.join(BUILD, "maininject" if "inject" in cfg else f"main{cfg['L']}")
    rx_env = {**os.environ, "INJECT_FER": str(cfg.get("inject", 0))}
    received = os.path.join(WORK, "received.gif")
    if os.path.exists(received):
        os.remove(received)
    tag = f"{sweep}_{point}_{run}"
    rx_path, tx_path = os.path.join(LOGS, tag + "_rx.log"), os.path.join(LOGS, tag + "_tx.log")
    with open(rx_path, "w") as rx_log, open(tx_path, "w") as tx_log:
        rx = subprocess.Popen([binary, RX_PORT, str(cfg["C"]), "rx", received],
                              stdout=rx_log, stderr=subprocess.STDOUT, cwd=WORK, env=rx_env)
        time.sleep(0.5)
        start = time.time()
        tx = subprocess.Popen([binary, TX_PORT, str(cfg["C"]), "tx", "penguin.gif"],
                              stdout=tx_log, stderr=subprocess.STDOUT, cwd=WORK)
        hung = False
        try:
            tx.wait(timeout=900)
        except subprocess.TimeoutExpired:
            hung = True
            stop(tx)
        wall = time.time() - start
        try:
            rx.wait(timeout=25)
        except subprocess.TimeoutExpired:
            stop(rx)
    tx_text, rx_text = open(tx_path, errors="replace").read(), open(rx_path, errors="replace").read()
    sent_ok = "File sent successfully" in tx_text
    close_ok = "llCloseTx failed" not in tx_text and "Transfer time" in tx_text
    file_ok = os.path.exists(received) and filecmp.cmp(SOURCE_FILE, received, shallow=False)
    if hung:
        outcome = "hung"
    elif not sent_ok:
        outcome = "failed"
    elif not file_ok:
        outcome = "corrupted"
    else:
        outcome = "complete"
    return {
        "sweep": sweep, "point": point, "run": run, "C": cfg["C"], "tprop_ms": cfg["tprop_ms"],
        "ber": f"{cfg['ber']:.10f}", "L": cfg["L"], "target_fer": cfg["target_fer"], "outcome": outcome,
        "tx_time_s": number(r"Transfer time:\s+([\d.]+)", tx_text, float),
        "frames_sent": number(r"I-frames sent:\s+(\d+)", tx_text),
        "retransmissions": number(r"Retransmissions:\s+(\d+)", tx_text),
        "timeouts": number(r"Timeouts:\s+(\d+)", tx_text),
        "rej": number(r"REJ received:\s+(\d+)", tx_text),
        "duplicates": number(r"Duplicates received:\s+(\d+)", rx_text),
        "rej_sent": number(r"REJ sent:\s+(\d+)", rx_text),
        "file_ok": int(file_ok), "close_ok": int(close_ok), "wall_s": f"{wall:.3f}",
    }


def main():
    out_path = sys.argv[1]
    # Optional: "smoke" for one baseline transfer, or a comma-separated list of sweeps to run
    only = sys.argv[2] if len(sys.argv) > 2 else None
    os.makedirs(LOGS, exist_ok=True)
    subprocess.run(["cp", SOURCE_FILE, os.path.join(WORK, "penguin.gif")], check=True)

    done = set()
    if os.path.exists(out_path):
        with open(out_path) as handle:
            done = {(r["sweep"], r["point"], r["run"]) for r in csv.DictReader(handle)}
    new_file = not os.path.exists(out_path)
    out = open(out_path, "a", newline="")
    writer = csv.DictWriter(out, FIELDS)
    if new_file:
        writer.writeheader()

    if only == "smoke":
        plan = [("smoke", dict(C=9600, tprop_ms=0, ber=0.0, L=1000, target_fer=""), 1)]
    elif only:
        plan = [step for step in build_plan() if step[0] in only.split(",")]
    else:
        plan = build_plan()

    counts = {}
    for sweep, cfg, runs in build_plan() if only and only != "smoke" else plan:
        if (sweep, cfg, runs) not in plan:
            counts[sweep] = counts.get(sweep, 0) + 1
            continue
        counts[sweep] = counts.get(sweep, 0) + 1
        point = counts[sweep]
        pending = [r for r in range(1, runs + 1) if (sweep, str(point), str(r)) not in done]
        if not pending:
            continue
        # One cable per point: its noise generator is unseeded, so repeats must share its stream
        cable = Cable(f"{sweep}_{point}")
        try:
            cable.configure(cfg)
            for run in pending:
                row = one_run(sweep, point, run, cfg)
                writer.writerow(row)
                out.flush()
                os.fsync(out.fileno())
                print(f"{sweep} point={point} run={run} C={cfg['C']} T={cfg['tprop_ms']} "
                      f"BER={cfg['ber']:.2e} L={cfg['L']} -> {row['outcome']} t={row['tx_time_s']} "
                      f"retx={row['retransmissions']} to={row['timeouts']} rej={row['rej']}", flush=True)
                time.sleep(2)  # let bytes still in flight leave the cable
        finally:
            cable.quit()
    print("ALL DONE", flush=True)


if __name__ == "__main__":
    main()
