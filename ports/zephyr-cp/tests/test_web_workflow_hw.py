# SPDX-FileCopyrightText: 2026 Mikey Sklar
# SPDX-License-Identifier: MIT

"""Web workflow regression tests against a real board.

The native_sim tests in test_web_workflow.py exercise the workflow over
hostnetwork only; nothing there touches a real radio, which is how the
listener-error bug fixed in siwx917/fix-web-workflow-listener-errors
survived upstream since 2023 (PR #7836). This module runs the
verified-working baseline from issue #10 against actual hardware.

These tests are skipped unless a board is provided via environment:

    CP_HW_BOARD_IP      board's IP address (required)
    CP_HW_WEB_PASSWORD  CIRCUITPY_WEB_API_PASSWORD value (required for /fs)
    CP_HW_SERIAL_PORT   serial port for the BLE-concurrency test (optional)

Run:

    CP_HW_BOARD_IP=... CP_HW_WEB_PASSWORD=... pytest -m hw test_web_workflow_hw.py
"""

from __future__ import annotations

import hashlib
import os
import time

import pytest
import requests

BOARD_IP = os.environ.get("CP_HW_BOARD_IP")
WEB_PASSWORD = os.environ.get("CP_HW_WEB_PASSWORD")
SERIAL_PORT = os.environ.get("CP_HW_SERIAL_PORT")

pytestmark = [
    pytest.mark.hw,
    pytest.mark.skipif(BOARD_IP is None, reason="CP_HW_BOARD_IP not set"),
]

TIMEOUT = 10.0

# A Session with keep-alive disabled. The shared-connection version of this
# was a workaround for the connection-pool exhaustion in issue #35; with
# NET_MAX_CONN=12 that is fixed and 200 consecutive fresh connections run
# clean. Reusing a pooled connection is now the liability instead: the board
# closes idle keep-alive, so the next request on a stale one dies with
# ECONNRESET (errno 54) rather than anything meaningful about the workflow.
# Deliberately NO shared Session. The pooled-connection version was a
# workaround for the pool exhaustion in issue #35; with NET_MAX_CONN=12 that
# is fixed and 200 consecutive fresh connections measure clean. Reusing a
# pooled connection is now the liability: the board closes idle keep-alive,
# so a request on a stale one dies with ECONNRESET (errno 54), which says
# nothing about the workflow under test.


def url(path):
    return f"http://{BOARD_IP}{path}"


def auth():
    return ("", WEB_PASSWORD)


def wait_until_stable(deadline_s=30.0, consecutive=3):
    """Block until the workflow answers `consecutive` times in a row.

    A filesystem write over /fs triggers CircuitPython's auto-reload, which
    restarts the VM and with it the web workflow. Any test that runs after one
    of those needs to let the board come back, or it measures the restart
    instead of whatever it was written to measure.
    """
    deadline = time.monotonic() + deadline_s
    streak = 0
    while time.monotonic() < deadline:
        try:
            if requests.get(url("/cp/version.json"), timeout=TIMEOUT).status_code == 200:
                streak += 1
                if streak >= consecutive:
                    return
            else:
                streak = 0
        except requests.RequestException:
            streak = 0
        time.sleep(0.5)
    raise AssertionError(f"workflow did not stabilise within {deadline_s}s")


def test_version_json_ok():
    """/cp/version.json responds 200 with sane identity fields, no auth."""
    response = requests.get(url("/cp/version.json"), timeout=TIMEOUT)
    assert response.status_code == 200
    payload = response.json()
    assert payload["board_id"] == "silabs_siwx917_dk2605a"
    assert payload["web_api_version"] >= 4
    assert payload["mcu_name"] == "siwg917m111mgtba"


def test_version_json_ip_populated():
    """ip should reflect the board's real DHCP-assigned address.

    Was empty on real hardware (mikeysklar/circuitpython#34): the internal
    wifi_radio_get_ipv4_address() used by web_workflow.c for this field was
    a leftover ESP-IDF stub always returning 0, separate from
    common_hal_wifi_radio_get_ipv4_address() (the Python-facing getter,
    already fixed) which reads the same address correctly.
    """
    payload = requests.get(url("/cp/version.json"), timeout=TIMEOUT).json()
    assert payload["ip"] == BOARD_IP


def test_version_json_mdns_fields_populated():
    """board_name and hostname come from the mDNS responder (mikeysklar/circuitpython#37).

    Fixed by common-hal/mdns/Server.c, built on Zephyr's CONFIG_MDNS_RESPONDER
    + DNS-SD. Only checks what web_workflow.c reads directly (hostname,
    instance_name) -- actual on-air mDNS resolution from another host is not
    covered here, see test_mdns_hostname_resolves_over_network below.
    """
    payload = requests.get(url("/cp/version.json"), timeout=TIMEOUT).json()
    assert payload["board_name"] != ""
    assert payload["hostname"] != ""


def _mdns_query_a(hostname, timeout_s=3.0):
    """Send one mDNS A query for `hostname` and return the responder's IP, or None.

    Talks raw multicast DNS (RFC 6762) instead of shelling out to a
    platform tool (`dns-sd`/`avahi-browse`/`dig`), so this runs the same way
    in CI and locally.
    """
    import socket
    import struct

    labels = hostname.split(".") + ["local"]
    qname = b"".join(bytes([len(label)]) + label.encode() for label in labels) + b"\x00"
    # Header: id=0, flags=0, 1 question, 0/0/0 answer/authority/additional.
    query = struct.pack(">HHHHHH", 0, 0, 1, 0, 0, 0)
    query += qname + struct.pack(">HH", 1, 1)  # QTYPE=A, QCLASS=IN

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(timeout_s)
    try:
        sock.sendto(query, ("224.0.0.251", 5353))
        deadline = time.monotonic() + timeout_s
        while time.monotonic() < deadline:
            try:
                data, (from_ip, _port) = sock.recvfrom(2048)
            except OSError:
                break
            if from_ip == BOARD_IP and len(data) > 12:
                return from_ip
        return None
    finally:
        sock.close()


@pytest.mark.xfail(
    reason="On-air mDNS resolution wasn't reproducible during development: this "
    "network's AP appears to block multicast between wireless clients (a "
    "control browse for _services._dns-sd._udp turned up nothing from any "
    "other device either, only the querying host's own entries). The "
    "responder itself is confirmed correctly configured -- see "
    "test_version_json_mdns_fields_populated and the Kconfig comment in "
    "boards/siwx917_dk2605a.conf. Flips to XPASS on a network that allows it.",
    strict=False,
)
def test_mdns_hostname_resolves_over_network():
    """<hostname>.local should resolve to the board's IP via real mDNS."""
    payload = requests.get(url("/cp/version.json"), timeout=TIMEOUT).json()
    hostname = payload["hostname"]
    assert hostname
    resolved = _mdns_query_a(hostname)
    assert resolved == BOARD_IP


def test_fs_requires_auth():
    """/fs/ without credentials is 401, never an open listing."""
    response = requests.get(url("/fs/"), timeout=TIMEOUT)
    assert response.status_code == 401


@pytest.mark.skipif(WEB_PASSWORD is None, reason="CP_HW_WEB_PASSWORD not set")
def test_fs_authenticated_listing():
    response = requests.get(url("/fs/"), auth=auth(), timeout=TIMEOUT)
    assert response.status_code == 200


@pytest.mark.skipif(WEB_PASSWORD is None, reason="CP_HW_WEB_PASSWORD not set")
def test_fs_put_get_delete_cycle():
    """PUT a probe file, read it back byte-exact, delete it, confirm gone."""
    body = (f"# web workflow hw probe {time.time()}\n" + "x" * 512).encode()
    digest = hashlib.sha256(body).hexdigest()

    response = requests.put(url("/fs/probe_hw_test.py"), auth=auth(), data=body, timeout=TIMEOUT)
    assert response.status_code in (201, 204)

    # A filesystem write triggers auto-reload; the workflow restarts with the
    # VM. Give follow-up requests a short retry window across the bounce.
    def get_with_retry(path, deadline_s=15.0):
        deadline = time.monotonic() + deadline_s
        while True:
            try:
                return requests.get(url(path), auth=auth(), timeout=TIMEOUT)
            except requests.ConnectionError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(1.0)

    response = get_with_retry("/fs/probe_hw_test.py")
    assert response.status_code == 200
    assert hashlib.sha256(response.content).hexdigest() == digest

    response = requests.delete(url("/fs/probe_hw_test.py"), auth=auth(), timeout=TIMEOUT)
    assert response.status_code == 204

    response = get_with_retry("/fs/probe_hw_test.py")
    assert response.status_code == 404


def test_http_latency_sane():
    """20 sequential version.json fetches all succeed and none stalls.

    A generous per-request bound: the point is catching a wedged listener or
    a starved socket pool, not benchmarking.
    """
    # Preceding tests write to /fs, which auto-reloads the board; measuring
    # through that restart would time the reboot, not the listener.
    wait_until_stable()

    worst = 0.0
    for _ in range(20):
        start = time.monotonic()
        response = requests.get(url("/cp/version.json"), timeout=TIMEOUT)
        elapsed = time.monotonic() - start
        assert response.status_code == 200
        worst = max(worst, elapsed)
    assert worst < 5.0, f"worst latency {worst:.2f}s"


@pytest.mark.skipif(SERIAL_PORT is None, reason="CP_HW_SERIAL_PORT not set")
def test_http_survives_concurrent_ble_gatt():
    """Sustained GATT reads concurrent with HTTP traffic; the #13 leftover.

    Starts a BLE GATT service + advertising on the board over raw REPL,
    connects from this host with bleak, then hammers GATT reads while running
    HTTP requests. Both sides must complete with zero failures. Covers the
    gap noted when #13 closed: coexistence was measured with BLE sampled, not
    stressed.
    """
    bleak = pytest.importorskip("bleak")
    serial = pytest.importorskip("serial")
    import asyncio

    setup = (
        b"import _bleio\n"
        b"svc = _bleio.Service(_bleio.UUID(0x1234))\n"
        b"chrc = _bleio.Characteristic.add_to_service(\n"
        b"    svc, _bleio.UUID(0x5678), max_length=20, fixed_length=False,\n"
        b"    properties=_bleio.Characteristic.READ | _bleio.Characteristic.WRITE,\n"
        b"    read_perm=_bleio.Attribute.OPEN, write_perm=_bleio.Attribute.OPEN,\n"
        b"    initial_value=b'hw-test')\n"
        b"_bleio.adapter.name = 'SiWx917-HWTEST'\n"
        b"adv = bytes([15, 0x09]) + b'SiWx917-HWTEST'\n"
        b"_bleio.adapter.start_advertising(adv, connectable=True)\n"
        b"print('ADV', _bleio.adapter.advertising)\n"
    )

    s = serial.Serial(SERIAL_PORT, 115200, timeout=2)
    try:
        time.sleep(0.3)
        s.write(b"\x03")
        time.sleep(1.5)
        s.read(s.in_waiting or 1)
        # Consume the "press any key" state before the raw-REPL handshake.
        s.write(b"\r\n")
        time.sleep(1.0)
        s.reset_input_buffer()
        s.write(b"\x01")
        time.sleep(0.4)
        s.read(s.in_waiting or 1)
        s.write(setup + b"\x04")
        time.sleep(3.0)
        out = s.read(s.in_waiting or 1)
        assert b"ADV True" in out, f"BLE setup failed: {out!r}"

        char_uuid = "00005678-0000-1000-8000-00805f9b34fb"
        results = {"gatt_reads": 0, "gatt_fails": 0, "http_ok": 0, "http_fails": 0}

        async def run():
            device = await bleak.BleakScanner.find_device_by_name("SiWx917-HWTEST", timeout=20.0)
            assert device is not None, "board not found over BLE"
            async with bleak.BleakClient(device) as client:
                deadline = time.monotonic() + 30.0

                async def gatt_hammer():
                    while time.monotonic() < deadline:
                        try:
                            await client.read_gatt_char(char_uuid)
                            results["gatt_reads"] += 1
                        except Exception:
                            results["gatt_fails"] += 1

                async def http_hammer():
                    while time.monotonic() < deadline:
                        try:
                            response = await asyncio.to_thread(
                                requests.get, url("/cp/version.json"), timeout=TIMEOUT
                            )
                            if response.status_code == 200:
                                results["http_ok"] += 1
                            else:
                                results["http_fails"] += 1
                        except Exception:
                            results["http_fails"] += 1
                        await asyncio.sleep(0.2)

                await asyncio.gather(gatt_hammer(), http_hammer())

        asyncio.run(run())

        assert results["gatt_fails"] == 0, results
        assert results["http_fails"] == 0, results
        # Sanity that the hammer actually hammered.
        assert results["gatt_reads"] > 50, results
        assert results["http_ok"] > 20, results
    finally:
        # Soft reboot so the board returns to its own code.py.
        s.write(b"\x02")
        time.sleep(0.2)
        s.write(b"\x04")
        s.close()


@pytest.mark.skipif(SERIAL_PORT is None, reason="CP_HW_SERIAL_PORT not set")
def test_wifi_radio_addresses():
    """wifi.radio.addresses is a Sequence[str], never None.

    common_hal_wifi_radio_get_addresses() returned mp_const_none, wrong type
    for the shared-bindings contract ("addresses: Sequence[str] ... Empty
    sequence when not connected"). Same address as
    wifi_radio_get_ipv4_address(), formatted as a string tuple instead of
    reusing that raw getter directly.
    """
    serial = pytest.importorskip("serial")
    s = serial.Serial(SERIAL_PORT, 115200, timeout=2)
    try:
        time.sleep(0.3)
        s.write(b"\x03")
        time.sleep(1.5)
        s.read(s.in_waiting or 1)
        s.write(b"\r\n")
        time.sleep(1.0)
        s.reset_input_buffer()
        s.write(b"\x01")
        time.sleep(0.4)
        s.read(s.in_waiting or 1)
        s.write(
            b"import wifi\n"
            b"a = wifi.radio.addresses\n"
            b"print('TYPE', type(a).__name__)\n"
            b"print('LEN', len(a))\n"
            b"print('VAL', a[0] if a else None)\n"
            b"\x04"
        )
        time.sleep(2.0)
        out = s.read(s.in_waiting or 1).decode(errors="replace")
    finally:
        s.write(b"\x02")
        time.sleep(0.2)
        s.write(b"\x04")
        s.close()

    assert "TYPE tuple" in out, out
    assert "LEN 1" in out, out
    assert f"VAL {BOARD_IP}" in out, out
