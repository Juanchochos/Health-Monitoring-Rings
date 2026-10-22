#!/usr/bin/env python3
"""Capture the firmware's CSV stream to files.

Raw mode (Phases 2-3, Phase 4 raw): "timestamp_us,ecg,red,ir" rows go to the
output CSV. Phase 4 vitals mode: "V,..." rows go to <out>_vitals.csv and
"B,..." rows to <out>_beats.csv (prefix stripped, header added). Lines
starting with '#' are echoed to the console and saved to <out>.log.

    python tools/serial_logger.py /dev/ttyACM0 -o recordings/rest.csv -d 120
"""

from __future__ import annotations

import argparse
import pathlib
import sys
import time

import serial

VITALS_HEADER = "t_us,hr_bpm,pr_bpm,spo2_pct,r_ratio,pat_foot_us,pat_peak_us,flags"
BEATS_HEADER = "r_peak_t_us,rr_us,pat_foot_us,pat_peak_us,flags"


class LazyCsv:
    """Creates the file (with header) only when the first row arrives."""

    def __init__(self, path: pathlib.Path, header: str | None = None):
        self.path = path
        self.header = header
        self.file = None
        self.rows = 0

    def write(self, row: str) -> None:
        if self.file is None:
            self.file = open(self.path, "w", newline="")
            if self.header:
                self.file.write(self.header + "\n")
        self.file.write(row + "\n")
        self.rows += 1

    def close(self) -> None:
        if self.file:
            self.file.close()


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("port", help="serial port, e.g. /dev/ttyACM0 or COM5")
    parser.add_argument("-b", "--baud", type=int, default=115200)
    parser.add_argument("-o", "--out", type=pathlib.Path,
                        default=pathlib.Path(f"recordings/capture_{time.strftime('%Y%m%d_%H%M%S')}.csv"))
    parser.add_argument("-d", "--duration", type=float, default=0.0,
                        help="seconds to record (0 = until Ctrl+C)")
    args = parser.parse_args()

    args.out.parent.mkdir(parents=True, exist_ok=True)
    stem = args.out.with_suffix("")
    raw = LazyCsv(args.out)
    vitals = LazyCsv(pathlib.Path(f"{stem}_vitals.csv"), VITALS_HEADER)
    beats = LazyCsv(pathlib.Path(f"{stem}_beats.csv"), BEATS_HEADER)
    log_path = args.out.with_suffix(".log")

    start = time.monotonic()
    with serial.Serial(args.port, args.baud, timeout=1) as port, open(log_path, "w") as log_file:
        print(f"Recording {args.port} -> {args.out} (Ctrl+C to stop)")
        try:
            while args.duration <= 0 or time.monotonic() - start < args.duration:
                line = port.readline().decode("ascii", errors="replace").strip()
                if not line:
                    continue
                if line.startswith("#"):
                    print(line)
                    log_file.write(line + "\n")
                elif line.startswith("V,"):
                    vitals.write(line[2:])
                    print(line)
                elif line.startswith("B,"):
                    beats.write(line[2:])
                else:
                    raw.write(line)
        except KeyboardInterrupt:
            pass
        finally:
            for f in (raw, vitals, beats):
                f.close()

    elapsed = time.monotonic() - start
    for f in (raw, vitals, beats):
        if f.rows:
            print(f"Saved {f.rows} rows to {f.path}")
    print(f"Recorded {elapsed:.1f} s")
    return 0


if __name__ == "__main__":
    sys.exit(main())
