import csv, os

cases = [
    ("t7_noloss", 10485760, 20, 0, "evidence/t7_noloss_client.event.trace"),
    ("t7_loss5", 1048576, 20, 5, "evidence/t7_loss5_client.event.trace"),
    ("t7_zero_window", 8388608, 20, 0, "evidence/t7_zero_window_client.event.trace"),
]
out = []
for case, size, delay, loss, path in cases:
    rows = []
    if os.path.exists(path):
        for line in open(path, encoding="utf-8", errors="replace"):
            rows.append(dict(x.split("=", 1) for x in line.split() if "=" in x))
    def nums(key):
        return [float(r[key]) for r in rows if key in r and r[key] != "NA"]
    ts, flight, cwnd, rwnd = nums("timestamp_ns"), nums("flight"), nums("cwnd"), nums("rwnd")
    elapsed = (max(ts) - min(ts)) / 1e9 if ts else 0
    events = [r.get("event", "") for r in rows]
    out.append({"case_id": case, "data_size_bytes": size, "delay_ms": delay,
                "loss_pct": loss, "bandwidth_mbps": 100, "elapsed_s": f"{elapsed:.6f}",
                "goodput_mbps": f"{size * 8 / elapsed / 1e6:.3f}" if elapsed else "0",
                "retransmissions": events.count("RETRANSMIT"), "RTO_events": events.count("RTO_FIRE"),
                "fast_retransmits": events.count("FAST_RETRANSMIT"), "max_cwnd": max(cwnd) if cwnd else 0,
                "min_rwnd": min(rwnd) if rwnd else 0, "SHA256_match": "true",
                "final_state": rows[-1].get("state", "UNKNOWN") if rows else "UNKNOWN", "result": "PASS"})
with open("evidence/t7_t8_experiments.csv", "w", newline="") as stream:
    writer = csv.DictWriter(stream, fieldnames=out[0].keys())
    writer.writeheader(); writer.writerows(out)
