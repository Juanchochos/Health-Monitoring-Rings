#!/usr/bin/env python3
"""Live plot of the raw CSV stream (ECG, PPG Red, PPG IR).

Accepts "timestamp,ecg,red,ir" lines and plotter-mode "ecg,red,ir" lines;
anything else (headers, '#' diagnostics, vitals lines) is ignored.

    python tools/plot_serial.py /dev/ttyACM0 --seconds 5
"""

import argparse
import collections
import sys
import threading

import matplotlib.animation as animation
import matplotlib.pyplot as plt
import serial

ECG_RATE_HZ = 256


def parse(line: str):
    parts = line.split(",")
    try:
        values = [float(p) for p in parts]
    except ValueError:
        return None
    if len(values) == 4:
        return values[1:]
    if len(values) == 3:
        return values
    return None


def reader(port: serial.Serial, rows: collections.deque, stop: threading.Event) -> None:
    while not stop.is_set():
        raw = port.readline()
        if not raw:
            continue
        sample = parse(raw.decode("ascii", errors="replace").strip())
        if sample is not None:
            rows.append(sample)


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    parser.add_argument("-s", "--seconds", type=float, default=5.0, help="visible window")
    args = parser.parse_args()

    window = int(args.seconds * ECG_RATE_HZ)
    rows = collections.deque(maxlen=window)
    stop = threading.Event()

    port = serial.Serial(args.port, args.baud, timeout=0.5)
    thread = threading.Thread(target=reader, args=(port, rows, stop), daemon=True)
    thread.start()

    fig, axes = plt.subplots(3, 1, sharex=True, figsize=(10, 7))
    titles = ("ECG (MAX30003 counts)", "PPG Red (counts)", "PPG IR (counts)")
    lines = []
    for ax, title in zip(axes, titles):
        (line,) = ax.plot([], [], linewidth=0.8)
        ax.set_title(title, fontsize=9)
        ax.grid(True, alpha=0.3)
        lines.append(line)
    axes[-1].set_xlabel("samples (ECG rate)")

    def update(_frame):
        snapshot = list(rows)
        if not snapshot:
            return lines
        x = range(len(snapshot))
        for idx, (line, ax) in enumerate(zip(lines, axes)):
            y = [row[idx] for row in snapshot]
            line.set_data(x, y)
            ax.set_xlim(0, max(window, 1))
            lo, hi = min(y), max(y)
            pad = (hi - lo) * 0.1 or 1.0
            ax.set_ylim(lo - pad, hi + pad)
        return lines

    _anim = animation.FuncAnimation(fig, update, interval=100, cache_frame_data=False)
    try:
        plt.tight_layout()
        plt.show()
    finally:
        stop.set()
        thread.join(timeout=1)
        port.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
