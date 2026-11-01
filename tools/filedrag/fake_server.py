#!/usr/bin/env python3
# SPDX-FileCopyrightText: (C) 2026 Lucas Magotteaux
# SPDX-License-Identifier: MIT
"""Minimal Deskflow protocol 1.9 server for testing the client half of file
drag and drop on one machine (no TLS).

It performs the handshake, announces a file drag (DGBN), enters the client
screen, streams the files with the same ack window as ClientProxy1_9, moves
the cursor to the drop point and releases the button.

usage: fake_server.py --port 24991 --enter 1150,700 --drop 940,400 FILE...
       [--release-early]   release the button right after entering (cancel case)
"""
import argparse
import os
import socket
import struct
import sys
import threading
import time

CHUNK = 64 * 1024
WINDOW = 8
T0 = time.time()


def log(msg):
    print(f"[server {time.time() - T0:6.3f}] {msg}", flush=True)


def s(data: bytes) -> bytes:  # %s
    return struct.pack(">I", len(data)) + data


class Conn:
    def __init__(self, sock):
        self.sock = sock
        self.lock = threading.Lock()
        self.acked = {}      # per transfer id
        self.cancelled = {}  # per transfer id
        self.cv = threading.Condition()
        # reverse mode (client -> server)
        self.client_dragging = False
        self.incoming = None  # dict(id, entries, out_dir, chunks, done_files)
        self.incoming_done = threading.Event()
        self.out_dir = None

    def send(self, payload: bytes):
        with self.lock:
            self.sock.sendall(struct.pack(">I", len(payload)) + payload)

    def recv_exact(self, n):
        buf = b""
        while len(buf) < n:
            part = self.sock.recv(n - len(buf))
            if not part:
                raise EOFError
            buf += part
        return buf

    def recv(self):
        (n,) = struct.unpack(">I", self.recv_exact(4))
        return self.recv_exact(n)


def build_entries(paths):
    """(relpath, size, is_dir, source) like deskflow::filedrag::buildManifest."""
    entries = []
    for p in paths:
        top = os.path.basename(os.path.normpath(p))
        if os.path.isdir(p) and not os.path.islink(p):
            entries.append((top, 0, True, p))
            stack = [(p, top)]
            while stack:
                d, rel = stack.pop()
                subdirs = []
                for name in sorted(os.listdir(d)):
                    full = os.path.join(d, name)
                    if os.path.islink(full):
                        continue
                    if os.path.isdir(full):
                        entries.append((rel + "/" + name, 0, True, full))
                        subdirs.append((full, rel + "/" + name))
                    elif os.path.isfile(full):
                        entries.append((rel + "/" + name, os.path.getsize(full), False, full))
                stack.extend(reversed(subdirs))
        elif os.path.isfile(p):
            entries.append((top, os.path.getsize(p), False, p))
    return entries


def parse_manifest(data):
    magic, count = struct.unpack(">II", data[:8])
    assert magic == 0x44464D32, "bad manifest"
    pos, out = 8, []
    for _ in range(count):
        t, size, n = struct.unpack(">BQI", data[pos:pos + 13])
        pos += 13
        out.append((data[pos:pos + n].decode(), size, t == 1))
        pos += n
    return out


def manifest(entries):
    out = struct.pack(">II", 0x44464D32, len(entries))
    for rel, size, is_dir, _ in entries:
        b = rel.encode()
        out += struct.pack(">BQI", 1 if is_dir else 0, size, len(b)) + b
    return out


def reader(conn: Conn):
    try:
        while True:
            msg = conn.recv()
            code = msg[:4]
            if code in (b"CALV", b"CNOP"):
                continue
            if code == b"DGAK":
                tid, chunks = struct.unpack(">II", msg[4:12])
                with conn.cv:
                    conn.acked[tid] = chunks
                    conn.cv.notify_all()
            elif code == b"DGST":
                conn.client_dragging = msg[4] != 0
                log(f"client drag state: {'dragging' if conn.client_dragging else 'not dragging'}")
                with conn.cv:
                    conn.cv.notify_all()
            elif code == b"DGBN":
                tid = struct.unpack(">I", msg[4:8])[0]
                (mlen,) = struct.unpack(">I", msg[8:12])
                entries = parse_manifest(msg[12:12 + mlen])
                files = sum(1 for e in entries if not e[2])
                log(f"incoming transfer {tid:#x}: {len(entries)} entries, {files} files")
                for rel, size, is_dir in entries:
                    if is_dir:
                        os.makedirs(os.path.join(conn.out_dir, rel), exist_ok=True)
                conn.incoming = dict(id=tid, entries=entries, chunks=0, files_left=files, fh=None, cur=None)
                if files == 0:
                    conn.incoming_done.set()
            elif code == b"DGDT":
                tid, entry = struct.unpack(">II", msg[4:12])
                (dlen,) = struct.unpack(">I", msg[12:16])
                inc = conn.incoming
                if inc["cur"] != entry:
                    if inc["fh"]:
                        inc["fh"].close()
                    inc["fh"] = open(os.path.join(conn.out_dir, inc["entries"][entry][0]), "wb")
                    inc["cur"] = entry
                inc["fh"].write(msg[16:16 + dlen])
                inc["chunks"] += 1
                if inc["chunks"] % 4 == 0:
                    conn.send(b"DGAK" + struct.pack(">II", tid, inc["chunks"]))
            elif code == b"DGFE":
                tid, entry = struct.unpack(">II", msg[4:12])
                inc = conn.incoming
                path = os.path.join(conn.out_dir, inc["entries"][entry][0])
                if inc["cur"] == entry and inc["fh"]:
                    inc["fh"].close()
                    inc["fh"] = None
                    inc["cur"] = None
                elif not os.path.exists(path):
                    open(path, "wb").close()
                conn.send(b"DGAK" + struct.pack(">II", tid, inc["chunks"]))
                inc["files_left"] -= 1
                if inc["files_left"] == 0:
                    log(f"incoming transfer complete ({inc['chunks']} chunks)")
                    conn.incoming_done.set()
            elif code == b"DGCN":
                tid, reason = struct.unpack(">IB", msg[4:9])
                log(f"client cancelled transfer {tid} (reason {reason})")
                with conn.cv:
                    conn.cancelled[tid] = reason
                    conn.cv.notify_all()
            else:
                log(f"recv {code!r} ({len(msg)} bytes)")
    except (EOFError, OSError):
        log("client disconnected")


def keepalive(conn: Conn, stop):
    while not stop.is_set():
        try:
            conn.send(b"CALV")
        except OSError:
            return
        time.sleep(1)


def stream_files(conn: Conn, tid, entries, delay=0.0):
    sent = 0
    for index, (_, _, is_dir, path) in enumerate(entries):
        if is_dir:
            continue
        with open(path, "rb") as f:
            while True:
                data = f.read(CHUNK)
                if not data:
                    break
                with conn.cv:
                    conn.cv.wait_for(lambda: sent - conn.acked.get(tid, 0) < WINDOW or tid in conn.cancelled, timeout=30)
                    if tid in conn.cancelled:
                        log(f"stop sending transfer {tid} (cancelled)")
                        return
                conn.send(b"DGDT" + struct.pack(">II", tid, index) + s(data))
                sent += 1
                if delay:
                    time.sleep(delay)
        conn.send(b"DGFE" + struct.pack(">II", tid, index))
        log(f"transfer {tid}: file {index} sent")
    log(f"transfer {tid}: all files sent ({sent} chunks)")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", type=int, default=24991)
    ap.add_argument("--enter", default="1150,700")
    ap.add_argument("--drop", default="940,400")
    ap.add_argument("--release-early", action="store_true")
    ap.add_argument("--key-after-drop", type=lambda v: int(v, 0), default=None,
                    help="KeyID to press after the drop (e.g. 0xEFE1 = left shift)")
    ap.add_argument("--chunk-delay", type=float, default=0.0, help="seconds between chunks (simulate a slow link)")
    ap.add_argument("--second", nargs="+", metavar="FILE",
                    help="after the first drop: leave, then drag these too while the first transfer still runs")
    ap.add_argument("--second-delay", type=float, default=1.5)
    ap.add_argument("--reverse", metavar="OUT_DIR",
                    help="client -> server: press at --enter, drag towards --drop, ask for the files, store them here")
    ap.add_argument("files", nargs="*")
    a = ap.parse_args()
    ex, ey = map(int, a.enter.split(","))
    dx, dy = map(int, a.drop.split(","))

    srv = socket.socket()
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("127.0.0.1", a.port))
    srv.listen(1)
    srv.settimeout(20)
    log(f"listening on {a.port}")
    sock, _ = srv.accept()
    sock.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    conn = Conn(sock)

    conn.send(b"Synergy" + struct.pack(">hh", 1, 9))
    hello = conn.recv()
    major, minor = struct.unpack(">hh", hello[7:11])
    log(f"client hello back: {hello[:7]!r} {major}.{minor} name={hello[15:].decode()!r}")
    conn.send(b"QINF")
    info = conn.recv()
    log(f"client info: {info[:4]!r} shape={struct.unpack('>hhhh', info[4:12])}")
    conn.send(b"CIAK")
    conn.send(b"CROP")
    conn.send(b"DSOP" + struct.pack(">I", 0))

    stop = threading.Event()
    threading.Thread(target=reader, args=(conn,), daemon=True).start()
    threading.Thread(target=keepalive, args=(conn, stop), daemon=True).start()
    time.sleep(0.5)

    if a.reverse:
        return reverse(conn, a, ex, ey, dx, dy, stop)

    tid = 1
    entries = build_entries(a.files)
    log(f"drag begin: {len(entries)} entries from {[os.path.basename(os.path.normpath(p)) for p in a.files]}")
    conn.send(b"DGBN" + struct.pack(">I", tid) + s(manifest(entries)))
    conn.send(b"CINN" + struct.pack(">hhIH", ex, ey, 1, 0))
    sender = threading.Thread(target=stream_files, args=(conn, tid, entries, a.chunk_delay), daemon=True)
    sender.start()

    if a.release_early:
        time.sleep(0.3)
        conn.send(b"DMMV" + struct.pack(">hh", ex + 40, ey))
        time.sleep(0.2)
        log("release over nothing")
        conn.send(b"DMUP" + struct.pack(">B", 1))
    else:
        # move like a user would, starting immediately (tests the deferred moves)
        steps = 45
        for i in range(1, steps + 1):
            x = ex + (dx - ex) * i // steps
            y = ey + (dy - ey) * i // steps
            conn.send(b"DMMV" + struct.pack(">hh", x, y))
            time.sleep(0.015)
        for i in range(20):
            conn.send(b"DMMV" + struct.pack(">hh", dx + (1 if i % 2 else -1), dy))
            time.sleep(0.05)
        log("release (drop)")
        conn.send(b"DMUP" + struct.pack(">B", 1))
        if a.key_after_drop is not None:
            time.sleep(0.8)
            log(f"press key 0x{a.key_after_drop:04x}")
            conn.send(b"DKDL" + struct.pack(">HHH", a.key_after_drop, 0, 0x39) + s(b""))
            time.sleep(0.1)
            conn.send(b"DKUP" + struct.pack(">HHH", a.key_after_drop, 0, 0x39))

    if a.second:
        time.sleep(a.second_delay)
        log("leave, start a second drag on the server, come back")
        conn.send(b"COUT")
        time.sleep(0.5)
        entries2 = build_entries(a.second)
        conn.send(b"CINN" + struct.pack(">hhIH", ex, ey, 2, 0))
        conn.send(b"DGBN" + struct.pack(">I", 2) + s(manifest(entries2)))
        sender2 = threading.Thread(target=stream_files, args=(conn, 2, entries2, a.chunk_delay), daemon=True)
        sender2.start()
        steps = 45
        for i in range(1, steps + 1):
            conn.send(b"DMMV" + struct.pack(">hh", ex + (dx - ex) * i // steps, ey + (dy - ey) * i // steps))
            time.sleep(0.015)
        for i in range(20):
            conn.send(b"DMMV" + struct.pack(">hh", dx + (1 if i % 2 else -1), dy))
            time.sleep(0.05)
        log("release second drag (drop)")
        conn.send(b"DMUP" + struct.pack(">B", 1))
        sender2.join(timeout=120)
    sender.join(timeout=120)
    log("sender finished")
    time.sleep(1.5)
    conn.send(b"COUT")
    time.sleep(0.3)
    conn.send(b"CBYE")
    stop.set()
    time.sleep(0.3)
    sock.close()
    log("done")


def reverse(conn, a, ex, ey, dx, dy, stop):
    """Client -> server: start a real drag on the client, then take it over."""
    conn.out_dir = a.reverse
    os.makedirs(conn.out_dir, exist_ok=True)
    conn.send(b"CINN" + struct.pack(">hhIH", ex, ey, 1, 0))
    time.sleep(0.3)
    log("press on the item")
    conn.send(b"DMDN" + struct.pack(">B", 1))
    time.sleep(0.25)
    steps = 30
    for i in range(1, steps + 1):  # drag towards the edge, like a user would
        conn.send(b"DMMV" + struct.pack(">hh", ex + (dx - ex) * i // steps, ey + (dy - ey) * i // steps))
        time.sleep(0.02)
    with conn.cv:
        conn.cv.wait_for(lambda: conn.client_dragging, timeout=5)
    if not conn.client_dragging:
        log("FAIL: client never reported a file drag")
        conn.send(b"DMUP" + struct.pack(">B", 1))
    else:
        log("edge reached: asking the client to hand over, then leaving it")
        conn.send(b"DGHO")
        conn.send(b"COUT")
        if conn.incoming_done.wait(timeout=60):
            log("all incoming files stored")
        else:
            log("FAIL: incoming transfer did not complete")
    time.sleep(1)
    conn.send(b"CBYE")
    stop.set()
    time.sleep(0.3)
    conn.sock.close()
    log("done")


if __name__ == "__main__":
    sys.exit(main())
