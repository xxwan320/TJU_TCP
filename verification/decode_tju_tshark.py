#!/usr/bin/env python3
"""Decode the fixed 20-byte TJU header from tshark TSV output."""
import csv
import socket
import struct
import sys


def main() -> int:
    """把 tshark 导出的 UDP payload 十六进制列解码为可审计 CSV。"""
    if len(sys.argv) != 3:
        print(f"usage: {sys.argv[0]} tshark.tsv decoded.csv", file=sys.stderr)
        return 2
    rows = []
    with open(sys.argv[1], "rb") as raw:
        prefix = raw.read(2)
    # Windows tshark 导出可能是 UTF-16，Linux/手工导出通常是 UTF-8。
    encoding = "utf-16" if prefix in (b"\xff\xfe", b"\xfe\xff") else "utf-8-sig"
    with open(sys.argv[1], encoding=encoding) as source:
        for line in source:
            columns = line.rstrip("\n\r").split("\t")
            if len(columns) != 7 or not columns[6]:
                continue
            payload = bytes.fromhex(columns[6])
            if len(payload) < 20:
                continue
            # ! 表示网络字节序；格式宽度总和严格等于固定 20-byte 头。
            src, dst, seq, ack, hlen, plen, flags, window, ext = struct.unpack(
                "!HHIIHHBHB", payload[:20]
            )
            rows.append(columns[:6] + [src, dst, seq, ack, hlen, plen,
                                       f"0x{flags:02x}", window, ext,
                                       len(payload)])
    with open(sys.argv[2], "w", newline="", encoding="utf-8") as target:
        writer = csv.writer(target)
        writer.writerow(["frame", "time_s", "ip_src", "ip_dst", "udp_src",
                         "udp_dst", "tju_src", "tju_dst", "seq", "ack",
                         "hlen", "plen", "flags", "window", "ext",
                         "captured_tju_bytes"])
        writer.writerows(rows)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
