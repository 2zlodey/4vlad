"""Plot BladeRF detector reports as PNG files (requires matplotlib)."""

import argparse
import csv
import re
from dataclasses import dataclass
from pathlib import Path

import matplotlib

matplotlib.use("Agg")
import matplotlib.pyplot as plt


@dataclass
class Report:
    path: Path
    channel: int
    band_ghz: tuple[float, float]
    dwell_ms: int
    rows: list[tuple[float, float, float, float]]


def read_report(path: Path) -> Report:
    text = path.read_text(encoding="utf-8-sig", errors="replace")
    settings = re.search(r"^Started job=\d+ channel=(\d+) .*?band=(\d+)\.\.(\d+) kHz dwell=(\d+) ms", text, re.M)
    completed = re.search(r"^progress=1000\D+? candidates=(\d+)\s*$", text, re.M)
    _, table_header, body = text.partition("Candidates (center MHz | width kHz | power dBFS | SNR dB)")
    if not (settings and completed and table_header):
        raise ValueError(f"{path}: expected a completed detector report")

    rows = []
    for fields in csv.reader(body.splitlines(), delimiter="|"):
        if not fields or not fields[0].strip():
            continue
        if fields[0].startswith("Scanner exited="):
            break
        if len(fields) != 4:
            raise ValueError(f"{path}: invalid candidate row: {fields!r}")
        center, width, power, snr = map(float, fields)
        rows.append((center / 1000, width, power, snr))

    expected = int(completed[1])
    if len(rows) != expected:
        raise ValueError(f"{path}: expected {expected} candidates, parsed {len(rows)}")
    channel, start, stop, dwell = map(int, settings.groups())
    return Report(path, channel, (start / 1e6, stop / 1e6), dwell, rows)


def plot_report(report: Report, band_ghz, limits) -> Path:
    figure, axes = plt.subplots(3, 1, sharex=True, figsize=(12, 8), layout="constrained")
    for axis, column, label, color, bounds in zip(
        axes, (2, 3, 1), ("Power (dBFS)", "SNR (dB)", "Bandwidth (kHz)"),
        ("#006b86", "#aa4828", "#3e6e45"), limits,
    ):
        axis.scatter([row[0] for row in report.rows], [row[column] for row in report.rows],
                     s=13, alpha=0.8, color=color, linewidths=0)
        axis.set_ylabel(label)
        axis.set_ylim(*bounds)
        axis.grid(alpha=0.22)
        axis.set_axisbelow(True)

    axes[-1].set(xlabel="Candidate center frequency (GHz)", xlim=band_ghz)
    start, stop = report.band_ghz
    figure.suptitle(
        f"BladeRF RX{report.channel + 1}  |  {start:g}-{stop:g} GHz  |  "
        f"dwell {report.dwell_ms} ms  |  {len(report.rows)} candidates"
    )
    output = report.path.with_suffix(".png")
    figure.savefig(output, dpi=160)
    plt.close(figure)
    return output


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("reports", nargs="+", type=Path, help="detector .txt reports; scales are shared across inputs")
    args = parser.parse_args()
    try:
        reports = [read_report(path) for path in args.reports]
    except (OSError, ValueError) as error:
        parser.error(str(error))

    band_ghz = min(report.band_ghz[0] for report in reports), max(report.band_ghz[1] for report in reports)
    limits = []
    for column in (2, 3, 1):
        values = [row[column] for report in reports for row in report.rows]
        low, high = (min(values), max(values)) if values else (0, 1)
        margin = max((high - low) * 0.06, 1)
        limits.append((low - margin, high + margin))

    for report in reports:
        output = plot_report(report, band_ghz, limits)
        print(f"{output}: {len(report.rows)} candidates")


if __name__ == "__main__":
    main()