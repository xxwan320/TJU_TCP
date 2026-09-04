#!/usr/bin/env python3
"""Plot flow-control and retransmission fields from a TJU event trace."""
import sys

import matplotlib.pyplot as plt


def parse(path):
    rows = []
    with open(path, encoding="utf-8", errors="replace") as stream:
        for line in stream:
            item = {}
            for token in line.split():
                if "=" in token:
                    key, value = token.split("=", 1)
                    item[key] = value
            if "timestamp_ns" in item:
                rows.append(item)
    return rows


def numbers(rows, key, default=0.0):
    values = []
    for row in rows:
        value = row.get(key, str(default))
        values.append(default if value == "NA" else float(value))
    return values


def main() -> int:
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} input.trace output.png", file=sys.stderr)
        return 2
    rows = parse(sys.argv[1])
    if not rows:
        print("trace contains no events", file=sys.stderr)
        return 1
    stride = max(1, len(rows) // 20000)
    rows = rows[::stride]
    start = int(rows[0]["timestamp_ns"])
    time_s = [(int(row["timestamp_ns"]) - start) / 1e9 for row in rows]

    fig, axes = plt.subplots(4, 1, figsize=(12, 12), sharex=True)
    axes[0].plot(time_s, numbers(rows, "rwnd"), label="peer rwnd")
    axes[0].plot(time_s, numbers(rows, "flight"), label="FlightSize")
    axes[0].set_ylabel("bytes")
    axes[0].legend()
    axes[0].grid(alpha=.25)

    axes[1].plot(time_s, numbers(rows, "cwnd"), label="cwnd")
    axes[1].plot(time_s, numbers(rows, "ssthresh"), label="ssthresh")
    axes[1].plot(time_s, [min(a, b) for a, b in zip(numbers(rows, "cwnd"), numbers(rows, "rwnd"))],
                 label="send limit=min(cwnd,rwnd)", linestyle="--")
    axes[1].set_ylabel("bytes")
    axes[1].legend()
    axes[1].grid(alpha=.25)

    # Mark Reno phases and loss recovery events directly from the trace.
    phase_colors = {"SLOW_START": "#d9f2d9", "CONGESTION_AVOIDANCE": "#d9e8ff",
                    "FAST_RECOVERY": "#ffe0b2"}
    for idx, row in enumerate(rows):
        phase = row.get("reno_phase")
        if phase in phase_colors:
            left = time_s[idx]
            right = time_s[idx + 1] if idx + 1 < len(time_s) else left
            axes[1].axvspan(left, right, color=phase_colors[phase], alpha=.18)
        if row.get("event") in {"FAST_RETRANSMIT", "RTO_FIRE"}:
            axes[1].axvline(time_s[idx], color="red" if row["event"] == "FAST_RETRANSMIT" else "black",
                            alpha=.55, linewidth=.8)
    axes[1].text(.01, .92, "green=slow start, blue=congestion avoidance, orange=fast recovery; vertical=FAST_RETRANSMIT/RTO",
                 transform=axes[1].transAxes, fontsize=8)

    axes[2].plot(time_s, numbers(rows, "recv_used"), label="receive buffer used")
    axes[2].set_ylabel("bytes")
    axes[2].legend()
    axes[2].grid(alpha=.25)

    axes[3].plot(time_s, numbers(rows, "rto"), label="RTO")
    axes[3].plot(time_s, numbers(rows, "srtt"), label="SRTT")
    axes[3].plot(time_s, numbers(rows, "rttvar"), label="RTTVAR")
    axes[3].set_xlabel("monotonic elapsed time (s)")
    axes[3].set_ylabel("seconds")
    axes[3].legend()
    axes[3].grid(alpha=.25)

    fig.tight_layout()
    fig.savefig(sys.argv[2], dpi=160)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
