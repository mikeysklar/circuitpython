// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Adafruit Industries
//
// SPDX-License-Identifier: MIT

// TEMPORARY diagnostic, not part of the port's real _bleio implementation.
//
// Minimal, fixed, compile-time GATT service used to test whether the
// BT_UUID_DECLARE_16-vs-BT_UUID_INIT_16 storage-duration bug (see
// mikeysklar/circuitpython#25) actually manifests on this board, and to
// give us something real to connect to before _bleio.Service/Characteristic
// are implemented (mikeysklar/circuitpython#2).
//
// Everything here is file-scope const/static, matching the fix Hermes
// identified: BT_UUID_INIT_16 has static storage duration, unlike
// BT_UUID_DECLARE_16 used inside a function (a compound literal, which
// only has automatic/stack storage duration at block scope). No Python
// objects are touched anywhere in this file, so the GC-safety concerns
// that apply to a real dynamic _bleio.Service implementation don't apply
// here -- this is intentionally scoped to just prove the storage-duration
// fix works on this hardware.

#include <zephyr/bluetooth/bluetooth.h>
#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>
#include <zephyr/logging/log.h>

#include "test_gatt_service.h"

LOG_MODULE_REGISTER(test_gatt_service, LOG_LEVEL_WRN);

// Vendor-specific 128-bit UUID for the test service itself (16-bit UUIDs
// are reserved by the Bluetooth SIG; a custom service must use a 128-bit
// UUID). The characteristic UUID is a plain 16-bit one to exercise the
// exact macro (BT_UUID_INIT_16) that BT_UUID_DECLARE_16 is unsafe outside
// of -- that's the actual thing under test here.
static const struct bt_uuid_128 test_svc_uuid = BT_UUID_INIT_128(
    0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
    0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f);

static const struct bt_uuid_16 test_chrc_uuid = BT_UUID_INIT_16(0x2a56);   // reuses the "Digital" characteristic UUID; value/meaning don't matter for this test

static const char test_chrc_value[] = "siwx917-gatt-test";

static ssize_t read_test_chrc(struct bt_conn *conn, const struct bt_gatt_attr *attr,
    void *buf, uint16_t len, uint16_t offset) {
    const char *value = attr->user_data;

    return bt_gatt_attr_read(conn, attr, buf, len, offset, value, strlen(value));
}

BT_GATT_SERVICE_DEFINE(test_svc,
    BT_GATT_PRIMARY_SERVICE(&test_svc_uuid),
    BT_GATT_CHARACTERISTIC(&test_chrc_uuid.uuid,
        BT_GATT_CHRC_READ,
        BT_GATT_PERM_READ,
        read_test_chrc, NULL, (void *)test_chrc_value),
);

// Hermes's 10-second check: after registration, every attribute's
// uuid->type must be a legal value (0 = 16-bit, 1 = 32-bit, 2 = 128-bit).
// Anything else means we're reading a dangling pointer -- no central
// connection needed to see it, it's visible in the RTT log.
//
// Call this after bt_enable() succeeds (not from SYS_INIT -- BT_GATT_SERVICE_DEFINE's
// STRUCT_SECTION_ITERABLE entry is only live/iterable once the Bluetooth
// host itself has initialized, which happens at bt_enable(), not at a fixed
// Zephyr boot stage).
void test_gatt_service_check(void) {
    LOG_WRN("test_gatt_service: %d attributes", test_svc.attr_count);
    for (size_t i = 0; i < test_svc.attr_count; i++) {
        const struct bt_gatt_attr *attr = &test_svc.attrs[i];
        uint8_t type = attr->uuid ? attr->uuid->type : 0xFF;
        const char *verdict = (type <= 2) ? "OK" : "GARBAGE -- DANGLING POINTER";
        LOG_WRN("  attr[%d] uuid->type=%d  %s", i, type, verdict);
    }
}
