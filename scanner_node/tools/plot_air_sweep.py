#!/usr/bin/env python3
"""Plot the measured BladeRF and HackRF air-sweep CSVs."""

import argparse
import csv
from pathlib import Path

import matplotlib.pyplot as plt


def read_measurements(path):
    frequencies = []
    powers = []
    gains = set()
    with path.open("r", newline="", encoding="utf-8-sig") as source:
        for row in csv.DictReader(source):
            frequencies.append(float(row["frequency_mhz"]))
            powers.append(float(row["power_dbfs"]))
            if row.get("frontend_gain_db"):
                gains.add(float(row["frontend_gain_db"]))
    return frequencies, powers, gains


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--data-dir",
        type=Path,
        default=Path(__file__).resolve().parents[1] / "measurements" / "air_sweep",
    )
    parser.add_argument(
        "--output",
        type=Path,
        default=None,
        help="PNG path (default: air_sweep_comparison.png in the data directory)",
    )
    args = parser.parse_args()

    files = {
        "BladeRF 2.0 micro": sorted(args.data_dir.glob("air_sweep_bladerf2_micro_*sample20MHz_grid20MHz_hackrfLNA*VGA*_*.csv")),
        "HackRF One": sorted(args.data_dir.glob("air_sweep_hackrf_one_*sample20MHz_grid20MHz_hackrfLNA*VGA*_*.csv")),
        "Stub SDR (deterministic simulator)": sorted(args.data_dir.glob("air_sweep_stub_sdr_*sample20MHz_grid20MHz_synthetic_*.csv")),
    }
    selected = {}
    for label, matches in files.items():
        if matches:
            selected[label] = matches[-1]
    if not selected:
        raise SystemExit("No compatible sweep CSV files found in {}".format(args.data_dir))

    figure, axis = plt.subplots(figsize=(15, 7), constrained_layout=True)
    colors = {
        "BladeRF 2.0 micro": "#176B87",
        "HackRF One": "#D97732",
        "Stub SDR (deterministic simulator)": "#438A5E",
    }
    for label, path in selected.items():
        frequencies, powers, gains = read_measurements(path)
        gain_label = "unknown" if len(gains) != 1 else "{:.2f} dB".format(next(iter(gains)))
        axis.plot(
            frequencies,
            powers,
            color=colors[label],
            linewidth=1.15,
            marker=".",
            markersize=2.8,
            label="{} (total gain {})".format(label, gain_label),
        )
    axis.set_xlabel("Frequency (MHz)")
    axis.set_ylabel("Power (dBFS)")
    axis.set_xlim(100, 6000)
    axis.grid(True, which="major", color="#d8dee4", linewidth=0.7)
    axis.legend(loc="lower right", frameon=True)
    axis.text(
        0.01,
        0.98,
        "296 points per trace | 20 MHz step | 2 MS/s | 1.75 MHz bandwidth",
        transform=axis.transAxes,
        horizontalalignment="left",
        verticalalignment="top",
        fontsize=9,
        color="#43505b",
    )
    title = "Stub SDR deterministic output | 100 MHz to 6 GHz" if len(selected) == 1 and next(iter(selected)).startswith("Stub SDR") else "RF power sweep | 100 MHz to 6 GHz"
    figure.suptitle(title, fontsize=16, fontweight="bold")

    output = args.output or args.data_dir / "air_sweep_comparison.png"
    figure.savefig(output, dpi=180, facecolor="white")
    print(output)


if __name__ == "__main__":
    main()
