# SPDX-FileCopyrightText: 2026 Mikey Sklar
# SPDX-License-Identifier: MIT

"""Socket-stack load tests against a real board.

REQUIRES HARDWARE. Nothing here runs against native_sim: it needs a real
radio, a real network between host and board, and Wi-Fi credentials in the
board's /settings.toml. The native_sim socket tests cannot reach these paths,
which is the whole reason this module exists -- the connection-refusal
behaviour in issue #35 is invisible without a real TCP stack under load.

Skipped unless a board is provided via environment:

    CP_HW_BOARD_IP      board's IP address (required)
    CP_HW_SERIAL_PORT   REPL serial port (required; used to run the server)

Run:

    CP_HW_BOARD_IP=192.168.0.39 CP_HW_SERIAL_PORT=/dev/cu.usbmodem0004403538361 \\
        pytest -m hw test_wifi_socket_hw.py

The fixture pastes hw_wifi_webserver.py into the board's REPL and holds the
serial port open for the duration, so the board's OWN log is captured and
asserted on. That matters: the difference between the two failure modes this
module distinguishes is only visible board-side. A host-only test can see that
connections were refused but not whether accept() ever ran out of descriptors.

For the measured findings and how to read a result, see hw_wifi_load.py.
"""

from __future__ import annotations

import os
import threading
import time

import pytest

from . import hw_wifi_load as load

BOARD_IP = os.environ.get("CP_HW_BOARD_IP")
SERIAL_PORT = os.environ.get("CP_HW_SERIAL_PORT")

pytestmark = [
    pytest.mark.hw,
    pytest.mark.skipif(BOARD_IP is None, reason="CP_HW_BOARD_IP not set"),
    pytest.mark.skipif(SERIAL_PORT is None, reason="CP_HW_SERIAL_PORT not set"),
]

HERE = os.path.dirname(os.path.abspath(__file__))
SERVER_SRC = os.path.join(HERE, "hw_wifi_webserver.py")

# Paste rate. The REPL has no flow control at 115200 and drops characters on
# long pastes at the more obvious 64 bytes / 50ms, which surfaces as a
# SyntaxError on a line that looks perfectly correct. See the module docstring
# in tools/ble-test/repl.py.
CHUNK_BYTES = 24
CHUNK_DELAY = 0.12


def _strip_source(text):
    """Drop the module docstring, comment lines and blanks.

    Purely a transfer-time optimisation: every byte costs 5ms on the wire at
    the rate above, and the server file is mostly comments. Logic untouched.
    """
    out = []
    in_doc = False
    doc_done = False
    for line in text.split("\n"):
        st = line.strip()
        if not doc_done and not in_doc and st.startswith('"""'):
            in_doc = True
            if st.endswith('"""') and len(st) > 3:
                in_doc, doc_done = False, True
            continue
        if in_doc:
            if st.endswith('"""'):
                in_doc, doc_done = False, True
            continue
        if not st or st.startswith("#"):
            continue
        out.append(line)
    return "\n".join(out)


class BoardServer:
    """The webserver running on the board, plus everything it printed."""

    def __init__(self, port_name):
        self._serial = pytest.importorskip("serial").Serial(port_name, 115200, timeout=0.3)
        self._buf = bytearray()
        self._lock = threading.Lock()
        self._stop = threading.Event()
        self._reader = None

    @property
    def log(self):
        with self._lock:
            return self._buf.decode("utf-8", "replace")

    def _pump(self):
        while not self._stop.is_set():
            b = self._serial.read(4096)
            if b:
                with self._lock:
                    self._buf.extend(b)

    def start(self, source, ready_marker="serving on", timeout=90.0):
        s = self._serial
        time.sleep(1.0)
        s.reset_input_buffer()
        s.write(b"\x03")  # interrupt whatever is running
        time.sleep(0.8)
        s.reset_input_buffer()
        s.write(b"\x05")  # paste mode
        time.sleep(0.6)

        data = source.encode() + b"\r\n"
        for i in range(0, len(data), CHUNK_BYTES):
            s.write(data[i : i + CHUNK_BYTES])
            s.flush()
            time.sleep(CHUNK_DELAY)
        time.sleep(0.8)
        s.write(b"\x04")  # execute

        self._reader = threading.Thread(target=self._pump, daemon=True)
        self._reader.start()

        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if ready_marker in self.log:
                return
            time.sleep(0.2)
        raise AssertionError(
            "board server did not start within %.0fs; log tail:\n%s" % (timeout, self.log[-2000:])
        )

    def stop(self):
        try:
            self._serial.write(b"\x03")
            time.sleep(0.5)
        except Exception:  # noqa: BLE001 - teardown must not mask a test failure
            pass
        self._stop.set()
        if self._reader is not None:
            self._reader.join(timeout=2.0)
        try:
            self._serial.close()
        except Exception:  # noqa: BLE001
            pass

    def accept_failures(self, kind=None):
        """ACCEPT FAILED lines from the board, optionally of one kind.

        An empty list for kind="DESCRIPTORS" is the positive evidence that the
        descriptor limit was never reached, which no host-side measurement can
        establish on its own.
        """
        lines = [ln for ln in self.log.splitlines() if ln.startswith("ACCEPT FAILED")]
        if kind is not None:
            lines = [ln for ln in lines if kind in ln]
        return lines


@pytest.fixture(scope="module")
def board_server():
    with open(SERVER_SRC) as f:
        source = _strip_source(f.read())

    server = BoardServer(SERIAL_PORT)
    try:
        server.start(source)
        # The listener is up, but give the stack a moment before the first
        # burst so connection setup is not competing with startup.
        time.sleep(0.5)
        yield server
    finally:
        server.stop()


def wait_until_stable(consecutive=3, deadline_s=30.0, quiet_s=3.0):
    """Answer `consecutive` requests in a row, then leave the board alone.

    The conns[] ceiling recovers in about a second, so a measurement taken
    straight after a burst measures the recovery rather than the thing under
    test.

    The quiet period at the end is not padding, and removing it makes the
    spaced-request tests flaky. Measured three ways on the same board:

      * idle 3s first, 200ms spacing, 5 trials   -> 100% every time
      * idle 3s first, 500ms spacing, 5 trials   -> 100% every time
      * probing right up to the measurement      -> 90%, 2 refused

    The probing needed to establish "the board is alive" is itself enough load
    to colour the next measurement, so prove liveness first and then stop
    talking to it. Same shape as wait_until_stable() in
    test_web_workflow_hw.py, which exists for the same reason after a
    filesystem write restarts the workflow.
    """
    deadline = time.monotonic() + deadline_s
    streak = 0
    while time.monotonic() < deadline:
        if load.one_request(BOARD_IP, load.DEFAULT_PORT, -1, 5.0).ok:
            streak += 1
            if streak >= consecutive:
                time.sleep(quiet_s)
                return
        else:
            streak = 0
        time.sleep(0.2)
    raise AssertionError("board did not settle within %.0fs" % deadline_s)


def test_server_reachable(board_server):
    """Fail loudly and early if the board is not serving.

    Without this, every later test reports a 0% success rate that reads
    exactly like a resource ceiling rather than a missing server.
    """
    result = load.probe(BOARD_IP)
    assert result.ok, "probe failed (%s: %s); board log tail:\n%s" % (
        result.error_class,
        result.detail,
        board_server.log[-1500:],
    )


def test_sequential_spaced_is_clean(board_server):
    """Spaced sequential requests must all succeed.

    This is the real regression guard, so the spacing is chosen to sit in the
    band that was measured clean rather than at the edge of it. 20 requests,
    6 trials at each spacing on an idle board:

        200ms    60, 60, 65, 65, 70, 60   -> NOT clean
        400ms   100 x 6                   -> clean
        800ms   100 x 6                   -> clean

    500ms is inside the clean band with margin on both sides. At 200ms the
    test's own TIME_WAIT backlog (1500ms per closed connection against
    NET_MAX_CONN=12) is enough to trip the ceiling, so a failure there would
    say nothing about a regression.
    """
    wait_until_stable()
    summary = load.run_sequential(BOARD_IP, count=20, spacing_ms=500)
    assert summary.success_rate == 100.0, "%s\nboard log tail:\n%s" % (
        summary,
        board_server.log[-1500:],
    )


def test_burst_never_exhausts_descriptors(board_server):
    """A concurrency sweep must not make accept() run out of descriptors.

    CONFIG_ZVFS_OPEN_MAX=8 is system-wide, and the listener plus the web
    workflow and mDNS sockets are already drawing on it. If that ever becomes
    the binding constraint, accept() raises EMFILE/ENFILE and the board logs it
    as an ACCEPT FAILED ... DESCRIPTORS line.

    Measured 2026-08-13: zero such lines across sweeps that served 128 and 53
    requests. Every failure was refused below accept(). This test is what keeps
    that true.
    """
    for level in (1, 2, 4, 6, 8, 12, 16):
        load.run_concurrent(BOARD_IP, concurrency=level)
        time.sleep(1.5)  # let conns[] recycle between levels

    descriptor_failures = board_server.accept_failures("DESCRIPTORS")
    assert not descriptor_failures, (
        "accept() hit the descriptor ceiling, which it never has before:\n%s"
        % "\n".join(descriptor_failures)
    )


def test_failures_are_rate_sensitive_not_concurrency_sensitive(board_server):
    """Spacing requests must help at least as much as it did when measured.

    The characterisation this module exists to defend: the ceiling is
    CONFIG_NET_MAX_CONN, which scales with request RATE and clears in about a
    second, not CONFIG_ZVFS_OPEN_MAX, which would scale with concurrency and
    not care about spacing. Measured 30% at 0ms against 100% at 200ms.

    Asserted as an inequality rather than against the 30% figure, because the
    unspaced number is a race and will move with network conditions. If
    spacing ever stops helping, the model is wrong and the comments in
    hw_wifi_load.py and hw_wifi_webserver.py need revisiting before anyone
    trusts them again.
    """
    # Settle before each half. Both must start from the same idle state or the
    # comparison measures leftover recovery from the previous phase instead of
    # the effect of spacing.
    wait_until_stable()
    unspaced = load.run_sequential(BOARD_IP, count=20, spacing_ms=0)
    wait_until_stable()
    spaced = load.run_sequential(BOARD_IP, count=20, spacing_ms=500)

    assert spaced.success_rate >= unspaced.success_rate, (
        "spacing made things worse, which contradicts the conns[] model\n"
        "unspaced: %s\nspaced:   %s" % (unspaced, spaced)
    )

    # Deliberately NOT asserting spaced == 100% here, unlike
    # test_sequential_spaced_is_clean. That test measures from an idle board
    # and does reach 100%. This one measures immediately after a deliberate
    # zero-spacing burst, and the board does not fully recover on that
    # timescale. Measured, 3 trials at each idle gap between the burst and the
    # spaced run:
    #
    #     idle  3s  ->  95, 95, 95
    #     idle  6s  -> 100, 95, 95
    #     idle 10s  ->  95, 95, 90
    #
    # More idle time does not help, so this is not a recovery window that can
    # be waited out. Asserting 100% here would be asserting something the
    # hardware does not do, and the test would fail intermittently for a
    # reason unrelated to what it is named for. The inequality above held in
    # 9/9 of those same trials, and it is the claim this test actually makes.
