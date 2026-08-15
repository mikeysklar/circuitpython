# SPDX-FileCopyrightText: 2026 Mikey Sklar
# SPDX-License-Identifier: MIT

"""Minimal CircuitPython HTTP server, for the socket load tests.

RUNS ON THE BOARD, not on the host. It is not a pytest module and pytest does
not collect it. test_wifi_socket_hw.py pastes it in over the REPL; it can also
be copied to code.py and run by hand.

socketpool only. No adafruit_httpserver, no external libraries -- this port has
no CIRCUITPY drive to install them onto.

Port 8080 on purpose. The web workflow owns 80, so binding 80 here would either
collide with it or silently displace its listener.

---------------------------------------------------------------------------
WHAT THE LOAD TESTS ACTUALLY MEASURED (2026-08-13)
---------------------------------------------------------------------------
Measured:

    sequential, 20 requests, 6 trials at each spacing, idle board:

        200ms    60, 60, 65, 65, 70, 60   -> NOT clean
        400ms   100 x 6                   -> clean
        800ms   100 x 6                   -> clean

Mechanism this fits: every request is a fresh connection, and the board holds
each closed one for CONFIG_NET_TCP_TIME_WAIT_DELAY=1500ms. At 200ms spacing
roughly 7.5 of those overlap, against CONFIG_NET_MAX_CONN=12 shared with the
listener and everything else; at 400ms it is under 4. So the ceiling is
reached by the test's own TIME_WAIT backlog, not by concurrency.

Rate-sensitive, not concurrency-sensitive. Across two full sweeps the board
served 128 and 53 requests with **zero** ACCEPT FAILED entries -- not one
EMFILE or ENFILE, ever. Every failure was an ECONNREFUSED generated below
accept(), so the application never saw it and descriptors were never the
ceiling. That signature is CONFIG_NET_MAX_CONN (Zephyr's connection.c conns[]
array), matching the recovery times already recorded in
boards/siwx917_dk2605a.conf: refused after 6 with ~1.11-1.14s recovery at
NET_MAX_CONN=8, clean at 12.

The discriminator, which is the part worth keeping:

  * Descriptor exhaustion scales with CONCURRENCY, fails immediately, does not
    recover, and is unaffected by spacing. It arrives as OSError EMFILE/ENFILE
    out of accept() -- i.e. it appears in THIS file's log, not just the host's.
  * conns[] exhaustion scales with REQUEST RATE and vanishes when requests are
    spaced >=400ms. It never reaches the application.

So: if the host reports failures and this log shows no ACCEPT FAILED lines, it
is not descriptors. Check spacing before blaming the firmware; below
400ms this harness cannot tell you anything about the firmware.

BACKLOG is a confound and is set deliberately -- see below.
"""

import gc
import os
import socketpool
import sys
import time
import wifi

PORT = 8080

# Must be >= the concurrency being measured, or the TEST is the ceiling rather
# than the firmware. net_tcp_listen() (zephyr subsys/net/ip/tcp.c:3859-3873)
# honours this value and clamps it to CONFIG_NET_MAX_CONN, so 12 is both the
# board's configured maximum and the largest value that means anything here.
# Measured: at BACKLOG=4 a concurrency-16 burst came back 10.9% successful with
# 57 ECONNREFUSED, which reads exactly like a firmware limit and is not one.
BACKLOG = 12

# Verified against the arm-none-eabi newlib headers this firmware is built
# with, not assumed: EAGAIN 11, ENFILE 23, EMFILE 24, ETIMEDOUT 116.
# py/mperrno.h defines MP_EAGAIN as 11 to match.
EAGAIN = 11
ENFILE = 23
EMFILE = 24
ETIMEDOUT = 116

# Small and fixed-length on purpose. A chunked or streaming body would add a
# failure mode of its own and confuse the resource picture.
BODY = (
    "<!doctype html><html><head><title>SiWx917</title></head>"
    "<body><h1>SiWx917 alive</h1><p>req #{n}</p></body></html>"
)


def ensure_wifi():
    """Associate if not already up. Returns the radio's IPv4 address."""
    if wifi.radio.connected and wifi.radio.ipv4_address is not None:
        print("wifi: already connected, ip=%s" % wifi.radio.ipv4_address)
        return wifi.radio.ipv4_address

    ssid = os.getenv("CIRCUITPY_WIFI_SSID")
    password = os.getenv("CIRCUITPY_WIFI_PASSWORD")
    if not ssid:
        raise RuntimeError(
            "no CIRCUITPY_WIFI_SSID in /settings.toml -- "
            "connect manually before starting the server"
        )

    print("wifi: connecting to %r ..." % ssid)
    t0 = time.monotonic()
    wifi.radio.connect(ssid, password)
    print("wifi: connected in %.2fs ip=%s" % (time.monotonic() - t0, wifi.radio.ipv4_address))
    return wifi.radio.ipv4_address


def read_request(conn, buf):
    """Read one HTTP request head. Returns the request line, or None."""
    n = 0
    # Bounded: only the request line matters, and reading a whole body would
    # let a slow client pin a descriptor we cannot spare.
    while n < len(buf):
        try:
            got = conn.recv_into(memoryview(buf)[n:])
        except OSError as e:
            print("  recv error: %r" % (e,))
            return None
        if got == 0:
            break
        n += got
        chunk = bytes(buf[:n])
        if b"\r\n\r\n" in chunk or b"\n\n" in chunk:
            break
    if n == 0:
        return None
    first = bytes(buf[:n]).split(b"\r\n", 1)[0]
    return first.decode("utf-8", "replace")


def serve():
    ip = ensure_wifi()
    pool = socketpool.SocketPool(wifi.radio)

    listener = pool.socket(pool.AF_INET, pool.SOCK_STREAM)
    # SO_REUSEADDR so a restart after a crash does not hit TIME_WAIT. Not every
    # CircuitPython build exposes it, hence the guard.
    try:
        listener.setsockopt(pool.SOL_SOCKET, pool.SO_REUSEADDR, 1)
    except (AttributeError, OSError) as e:
        print("note: SO_REUSEADDR unavailable (%r), continuing" % (e,))

    listener.bind(("0.0.0.0", PORT))
    listener.listen(BACKLOG)
    # Timeout rather than block forever, so ctrl-C reaches us between accepts.
    # 0.2s not 1.0s: socketpool_socket_accept() spins on RUN_BACKGROUND_TASKS
    # for the whole timeout window, so the board burns CPU between connections
    # and a long window adds noise to the low-concurrency latency numbers.
    listener.settimeout(0.2)

    print("=" * 60)
    print("serving on http://%s:%d/  (backlog=%d)" % (ip, PORT, BACKLOG))
    print("=" * 60)

    req_buf = bytearray(512)
    served = 0
    errors = 0

    # The accept loop is wrapped so the LISTENER is closed however we leave it.
    # Learned the hard way: a KeyboardInterrupt escaping the loop skipped
    # listener.close(), leaking the listening descriptor and leaving port 8080
    # bound. With ZVFS_OPEN_MAX=8 those leaks accumulate across runs and the
    # board eventually stops passing traffic while still reporting
    # wifi.radio.connected True -- which looks exactly like a firmware bug and
    # is not one. Never make this close skippable.
    try:
        served, errors = _accept_loop(listener, req_buf, served, errors)
    finally:
        print("shutting down: served=%d errors=%d" % (served, errors))
        try:
            listener.close()
        except OSError:
            pass


def _accept_loop(listener, req_buf, served, errors):
    while True:
        conn = None
        try:
            conn, addr = listener.accept()
        except OSError as e:
            err = getattr(e, "errno", None)

            if err == ETIMEDOUT:
                # Idle accept timeout. Normal, says nothing, do not log it.
                continue

            if err == EAGAIN:
                # NOT "would block". socketpool_socket_accept() returns
                # -MP_EAGAIN when mp_hal_is_interrupted() is true
                # (common-hal/socketpool/Socket.c:221-223), and
                # common_hal_socketpool_socket_accept() turns that into
                # OSError(EAGAIN). So ctrl-C during accept() arrives HERE and
                # never as KeyboardInterrupt.
                #
                # The listener always has a non-zero timeout, and the genuine
                # would-block case is swallowed by the retry loop inside
                # socketpool_socket_accept(), so EAGAIN reaching us is
                # unambiguously an interrupt. Counting it as a refusal would put
                # a failure that never happened into every hand-stopped run --
                # precisely the signal these tests exist to measure.
                print("interrupted during accept, stopping")
                break

            # Everything else is a real refusal from the board. Descriptor
            # exhaustion would arrive here as EMFILE/ENFILE from zsock_accept();
            # across every sweep run so far it never has, which is the evidence
            # that descriptors are not the ceiling.
            errors += 1
            kind = "DESCRIPTORS" if err in (EMFILE, ENFILE) else "other"
            print(
                "ACCEPT FAILED (#%d, %s errno=%s): %r  [served=%d free=%d]"
                % (errors, kind, err, e, served, gc.mem_free())
            )
            # Give the stack a moment to recycle whatever ran out.
            time.sleep(0.05)
            continue
        except KeyboardInterrupt:
            # Backstop only. The accept path signals interrupts via EAGAIN
            # above; this catches one raised anywhere else in the loop.
            break

        t0 = time.monotonic()
        try:
            conn.settimeout(2.0)
            line = read_request(conn, req_buf)
            served += 1
            body = BODY.format(n=served)
            resp = (
                "HTTP/1.1 200 OK\r\n"
                "Content-Type: text/html\r\n"
                "Content-Length: %d\r\n"
                "Connection: close\r\n"
                "\r\n%s" % (len(body), body)
            )
            conn.send(resp.encode("utf-8"))
            dt = (time.monotonic() - t0) * 1000
            print("#%-4d %s %-28s %6.1fms free=%d" % (served, addr[0], line, dt, gc.mem_free()))
        except Exception as e:  # noqa: BLE001
            # Deliberately not just OSError: a non-OSError escaping here would
            # kill the server mid-sweep and read as a board hang from the host
            # side, which is far more expensive to debug than one logged bad
            # request. Keep serving.
            errors += 1
            print("#%-4d SERVE FAILED: %r" % (served, e))
        finally:
            # Closing promptly is the whole game with 8 descriptors. Never let
            # this be skipped, even on an exception path.
            if conn is not None:
                try:
                    conn.close()
                except OSError:
                    pass

    return served, errors


if __name__ == "__main__":
    try:
        serve()
    except KeyboardInterrupt:
        print("interrupted")
    except Exception as e:  # noqa: BLE001 - want the traceback on the REPL
        print("fatal: %r" % (e,))
        sys.print_exception(e)
