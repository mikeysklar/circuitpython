# SPDX-FileCopyrightText: 2026 Mikey Sklar
# SPDX-License-Identifier: MIT

"""Host-side HTTP load generator for the board's socket stack.

Imported by test_wifi_socket_hw.py, and runnable on its own when you want the
full sweep printed rather than asserted:

    python -m tests.hw_wifi_load --host <board-ip>
    python -m tests.hw_wifi_load --host <board-ip> --concurrency 8
    python -m tests.hw_wifi_load --host <board-ip> --sequential 200 --spacing 0

(run from ports/zephyr-cp/, matching tests/perfetto_input_trace.py)

Standard library only, so it runs anywhere with a bare python3 -- including
from a second machine on the same LAN, which is how the same board gets
hammered from somewhere other than the developer's laptop.

Raw sockets rather than urllib on purpose: a refused connect, a reset
mid-response, a read timeout and a connection that opens and returns nothing
are four different findings, and urllib flattens all four into URLError. The
error-class breakdown is what identified the ceiling, so it is not cosmetic.

---------------------------------------------------------------------------
WHAT WAS MEASURED (2026-08-13), AND HOW TO READ A RESULT
---------------------------------------------------------------------------
The board's ceiling is CONFIG_NET_MAX_CONN, and it is RATE-sensitive.
Six-trial sampling:

    sequential, 20 requests, 6 trials at each spacing, idle board:

        200ms    60, 60, 65, 65, 70, 60   -> NOT clean
        400ms   100 x 6                   -> clean
        800ms   100 x 6                   -> clean

Mechanism this fits: every request is a fresh connection, and the board holds
each closed one for CONFIG_NET_TCP_TIME_WAIT_DELAY=1500ms. At 200ms spacing
roughly 7.5 of those overlap, against CONFIG_NET_MAX_CONN=12 shared with the
listener and everything else; at 400ms it is under 4. So the ceiling is
reached by the test's own TIME_WAIT backlog, not by concurrency.

It is NOT CONFIG_ZVFS_OPEN_MAX. Across two full sweeps the board served 128
and 53 requests with zero ACCEPT FAILED entries in its own log -- no EMFILE or
ENFILE, ever. Every failure was an ECONNREFUSED generated below accept(), so
the descriptor limit was never reached.

Telling the two apart, which is the reusable part:

  * Descriptor exhaustion scales with CONCURRENCY, is immediate, does not
    recover, and spacing does not help. It shows up as EMFILE/ENFILE in the
    BOARD's log, not only here.
  * conns[] exhaustion scales with REQUEST RATE and disappears at >=400ms
    spacing. It never reaches the board's application.

So a run that fails here while the board's log is clean of ACCEPT FAILED is
not a descriptor problem. Re-run with --spacing 400 before blaming firmware;
100ms and 200ms are both too fast to be clean on this board.

Watch the "first failure at" index and the error-class breakdown rather than
the success rate alone; the rate on its own cannot separate the two.

One confound to keep in mind: the board's listen backlog must be >= the
concurrency being measured or the TEST becomes the ceiling. At BACKLOG=4 a
concurrency-16 burst measured 10.9% successful with 57 ECONNREFUSED, which
looks exactly like a firmware limit. hw_wifi_webserver.py sets BACKLOG=12 for
this reason.
"""

from __future__ import annotations

import argparse
import socket
import statistics
import sys
import threading
import time

DEFAULT_PORT = 8080
DEFAULT_SWEEP = (1, 2, 4, 6, 7, 8, 12, 16)

REQUEST = (
    "GET /{path} HTTP/1.1\r\n"
    "Host: {host}\r\n"
    "User-Agent: siwx917-hw-load/1\r\n"
    "Connection: close\r\n"
    "\r\n"
)


class Result:
    __slots__ = ("index", "ok", "latency_ms", "error_class", "detail", "nbytes")

    def __init__(self, index, ok, latency_ms, error_class="", detail="", nbytes=0):
        self.index = index
        self.ok = ok
        self.latency_ms = latency_ms
        self.error_class = error_class
        self.detail = detail
        self.nbytes = nbytes


class Summary:
    """Outcome of one phase, in a form a test can assert on."""

    def __init__(self, label, results, elapsed_s):
        self.label = label
        self.results = results
        self.elapsed_s = elapsed_s
        self.ok = [r for r in results if r.ok]
        self.bad = [r for r in results if not r.ok]
        self.total = len(results)
        self.success_rate = (len(self.ok) / self.total * 100.0) if self.total else 0.0
        self.latencies = sorted(r.latency_ms for r in self.ok)
        self.first_failure = min((r.index for r in self.bad), default=None)
        self.error_classes = {}
        for r in self.bad:
            self.error_classes[r.error_class] = self.error_classes.get(r.error_class, 0) + 1

    def percentile(self, p):
        if not self.latencies:
            return float("nan")
        k = min(len(self.latencies) - 1, int(round((p / 100.0) * (len(self.latencies) - 1))))
        return self.latencies[k]

    def breakdown(self):
        return ", ".join("%s=%d" % kv for kv in sorted(self.error_classes.items()))

    def __str__(self):
        head = "%-22s %3d/%-3d  %6.1f%%  " % (
            self.label,
            len(self.ok),
            self.total,
            self.success_rate,
        )
        if self.latencies:
            head += "min %6.1f  med %6.1f  p95 %6.1f  max %7.1f ms  %5.1f req/s" % (
                self.latencies[0],
                statistics.median(self.latencies),
                self.percentile(95),
                self.latencies[-1],
                self.total / self.elapsed_s if self.elapsed_s > 0 else 0.0,
            )
        else:
            head += "no successful requests"
        if self.bad:
            head += "\n%-22s errors: %s | first failure at request #%s" % (
                "",
                self.breakdown(),
                self.first_failure,
            )
        return head


def classify(exc):
    """Map an exception to a short class name that says WHICH ceiling it is."""
    if isinstance(exc, socket.timeout):
        return "timeout"
    if isinstance(exc, ConnectionRefusedError):
        return "refused"
    if isinstance(exc, ConnectionResetError):
        return "reset"
    if isinstance(exc, OSError):
        # EHOSTUNREACH / ENETUNREACH / EADDRNOTAVAIL etc.
        return "oserror(%s)" % (exc.errno,)
    return type(exc).__name__


def one_request(host, port, index, timeout, path="index.html"):
    """Single HTTP GET over a fresh connection. Never raises."""
    t0 = time.perf_counter()
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.settimeout(timeout)
    try:
        s.connect((host, port))
        s.sendall(REQUEST.format(path=path, host=host).encode())

        chunks = []
        while True:
            b = s.recv(4096)
            if not b:
                break
            chunks.append(b)
        data = b"".join(chunks)
        dt = (time.perf_counter() - t0) * 1000.0

        if not data:
            # Connected but got nothing back. Distinct from a refusal: the SYN
            # was accepted into the backlog and then the connection died.
            return Result(index, False, dt, "empty", "connected, zero bytes")
        if not data.startswith(b"HTTP/1."):
            return Result(index, False, dt, "garbage", repr(data[:40]))
        if b" 200 " not in data.split(b"\r\n", 1)[0]:
            return Result(index, False, dt, "status", data.split(b"\r\n", 1)[0].decode("latin1"))
        return Result(index, True, dt, nbytes=len(data))
    except Exception as e:  # noqa: BLE001 - classification is the whole job
        dt = (time.perf_counter() - t0) * 1000.0
        return Result(index, False, dt, classify(e), str(e))
    finally:
        try:
            s.close()
        except OSError:
            pass


def run_sequential(host, port=DEFAULT_PORT, count=20, spacing_ms=0.0, timeout=5.0):
    """`count` requests one after another, `spacing_ms` apart."""
    results = []
    t0 = time.perf_counter()
    for i in range(count):
        results.append(one_request(host, port, i, timeout))
        if spacing_ms > 0 and i != count - 1:
            time.sleep(spacing_ms / 1000.0)
    return Summary("sequential %dms" % spacing_ms, results, time.perf_counter() - t0)


def run_concurrent(host, port=DEFAULT_PORT, concurrency=4, requests=None, timeout=5.0):
    """`requests` requests with `concurrency` in flight at once."""
    if requests is None:
        requests = max(concurrency * 4, 8)

    results = []
    lock = threading.Lock()
    counter = {"next": 0}
    # Release every thread at once so the burst is genuinely simultaneous.
    # Staggered starts would hide a concurrency-scaled ceiling.
    gate = threading.Event()

    def worker():
        while True:
            with lock:
                i = counter["next"]
                if i >= requests:
                    return
                counter["next"] = i + 1
            gate.wait()
            r = one_request(host, port, i, timeout)
            with lock:
                results.append(r)

    threads = [threading.Thread(target=worker, daemon=True) for _ in range(concurrency)]
    for t in threads:
        t.start()
    t0 = time.perf_counter()
    gate.set()
    for t in threads:
        t.join()
    return Summary("concurrency=%d" % concurrency, results, time.perf_counter() - t0)


def probe(host, port=DEFAULT_PORT, timeout=5.0):
    """One request, to tell 'server absent' from 'server struggling'.

    Without this a board that is simply not running the server reports a 0%
    success rate that looks exactly like a resource ceiling.
    """
    return one_request(host, port, -1, timeout)


def main(argv=None):
    ap = argparse.ArgumentParser(
        description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter
    )
    ap.add_argument("--host", required=True, help="board IPv4 address")
    ap.add_argument("--port", type=int, default=DEFAULT_PORT)
    ap.add_argument("--timeout", type=float, default=5.0, help="per-request socket timeout (s)")
    ap.add_argument("--sequential", type=int, default=20, help="sequential count (0 to skip)")
    ap.add_argument(
        "--spacing",
        type=float,
        default=0.0,
        help="ms between sequential requests; >=100 dodges the conns[] race",
    )
    ap.add_argument(
        "--concurrency",
        type=int,
        default=None,
        help="run one concurrency level instead of the sweep",
    )
    ap.add_argument(
        "--requests", type=int, default=None, help="requests per level (default: 4x the level)"
    )
    ap.add_argument("--sweep", default=",".join(str(x) for x in DEFAULT_SWEEP))
    ap.add_argument(
        "--settle", type=float, default=1.5, help="seconds between levels, to let conns[] recycle"
    )
    args = ap.parse_args(argv)

    print("=" * 78)
    print("siwx917 socket load -> %s:%d  (timeout %.1fs)" % (args.host, args.port, args.timeout))
    print("=" * 78)

    p = probe(args.host, args.port, args.timeout)
    if not p.ok:
        print("\nPROBE FAILED (%s): %s" % (p.error_class, p.detail))
        print("Server not reachable. Is hw_wifi_webserver.py running on the board?")
        return 2
    print("probe ok: %d bytes in %.1fms\n" % (p.nbytes, p.latency_ms))

    phases = []
    if args.sequential > 0:
        s = run_sequential(args.host, args.port, args.sequential, args.spacing, args.timeout)
        print("[sequential] %d requests, %.0fms spacing" % (args.sequential, args.spacing))
        print("  %s" % s)
        phases.append(s)

    levels = (
        [args.concurrency]
        if args.concurrency
        else [int(x) for x in args.sweep.split(",") if x.strip()]
    )
    if levels:
        print("\n[concurrent] burst levels: %s" % ", ".join(str(x) for x in levels))
        for c in levels:
            time.sleep(args.settle)
            s = run_concurrent(args.host, args.port, c, args.requests, args.timeout)
            print("  %s" % s)
            phases.append(s)

    print("\n" + "=" * 78)
    print("SUMMARY")
    print("=" * 78)
    for s in phases:
        print(
            "  %s %-18s %6.1f%%  (%d failed)"
            % ("ok  " if not s.bad else "FAIL", s.label, s.success_rate, len(s.bad))
        )

    broke = [s.label for s in phases if s.bad]
    print()
    if not broke:
        print("No failures at any level tested.")
    else:
        print("First phase to break: %s" % broke[0])
        print("Rate-sensitive and recovering in ~1s means conns[] (NET_MAX_CONN).")
        print("Concurrency-scaled with EMFILE/ENFILE in the BOARD's log means")
        print("descriptors (ZVFS_OPEN_MAX). Re-run with --spacing 100 to separate them.")
    return 0 if not broke else 1


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\ninterrupted")
        sys.exit(130)
