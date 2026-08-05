// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <string.h>

#include "py/runtime.h"
#include "shared-bindings/_bleio/UUID.h"

void common_hal_bleio_uuid_construct(bleio_uuid_obj_t *self, mp_int_t uuid16, const uint8_t uuid128[16]) {
    // DISCRIMINATE ON uuid128 == NULL, NOT ON uuid16 != 0.
    //
    // shared-bindings/_bleio/UUID.c:89 ALWAYS extracts
    // uuid16 = (uuid128[13] << 8) | uuid128[12] before calling here, then
    // zeroes those two bytes and passes the full 128-bit array as well. So for
    // a 128-bit UUID like 0000fa00-1212-efde-1523-785fef13d123, uuid16 is a
    // NONZERO 0xfa00 -- branching on it silently flattens every 128-bit UUID
    // into a 16-bit Bluetooth-SIG one.
    //
    // That registers without error (bt_gatt_service_register returns 0: the
    // entries were ACCEPTED, not verified to describe what was meant), and the
    // resulting attribute table advertises a SIG-assigned short UUID the
    // central never asked for. A strict central -- macOS is strict -- finds no
    // matching service, gives up, and terminates the link with
    // 0x13 REMOTE_USER_TERM.
    //
    // nordic gets this right at ports/nordic/common-hal/_bleio/UUID.c:26,
    // which keys off uuid128 == NULL. Match that.
    if (uuid128 == NULL) {
        // 16-bit UUID: expand against the Bluetooth Base UUID
        // 00000000-0000-1000-8000-00805F9B34FB.
        self->size = 16;
        const uint8_t base_uuid[16] = {0xfb, 0x34, 0x9b, 0x5f, 0x80, 0x00, 0x00, 0x80, 0x00, 0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
        memcpy(self->uuid128, base_uuid, 16);
        self->uuid128[12] = (uuid16 & 0xff);
        self->uuid128[13] = (uuid16 >> 8) & 0xff;
    } else {
        // 128-bit UUID. Bytes 12 and 13 arrive zeroed, so restore them from
        // the uuid16 the binding layer split out -- otherwise the stored UUID
        // is missing its two most distinctive bytes.
        self->size = 128;
        memcpy(self->uuid128, uuid128, 16);
        self->uuid128[12] = (uuid16 & 0xff);
        self->uuid128[13] = (uuid16 >> 8) & 0xff;
    }
}

uint32_t common_hal_bleio_uuid_get_uuid16(bleio_uuid_obj_t *self) {
    if (self->size == 16) {
        return (self->uuid128[13] << 8) | self->uuid128[12];
    }
    return 0;
}

void common_hal_bleio_uuid_get_uuid128(bleio_uuid_obj_t *self, uint8_t uuid128[16]) {
    memcpy(uuid128, self->uuid128, 16);
}

uint32_t common_hal_bleio_uuid_get_size(bleio_uuid_obj_t *self) {
    return self->size;
}

void common_hal_bleio_uuid_pack_into(bleio_uuid_obj_t *self, uint8_t *buf) {
    if (self->size == 16) {
        buf[0] = self->uuid128[12];
        buf[1] = self->uuid128[13];
    } else {
        memcpy(buf, self->uuid128, 16);
    }
}

const struct bt_uuid *bleio_uuid_as_bt_uuid(bleio_uuid_obj_t *self) {
    // Populate the PERSISTENT .bt member. A bt_gatt_attr stores only a pointer
    // to its uuid and Zephyr dereferences it on every ATT request for as long
    // as the service is registered, so building this on the stack at
    // registration time would leave dangling pointers in the attribute table.
    if (self->size == 16) {
        self->bt.u16.uuid.type = BT_UUID_TYPE_16;
        self->bt.u16.val = (uint16_t)((self->uuid128[13] << 8) | self->uuid128[12]);
    } else {
        self->bt.u128.uuid.type = BT_UUID_TYPE_128;
        memcpy(self->bt.u128.val, self->uuid128, 16);
    }
    return &self->bt.uuid;
}
