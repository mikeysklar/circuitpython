// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"

#include <zephyr/bluetooth/uuid.h>

typedef struct {
    mp_obj_base_t base;
    uint8_t uuid128[16];
    uint8_t size;
    // Persistent Zephyr UUID. bt_gatt_attr keeps a POINTER to the uuid for the
    // whole lifetime of a registered service, so it cannot be a stack
    // temporary built at registration time -- it has to live in the object.
    union {
        struct bt_uuid uuid;
        struct bt_uuid_16 u16;
        struct bt_uuid_128 u128;
    } bt;
} bleio_uuid_obj_t;

// Fill in the persistent .bt member from uuid128/size and return it.
const struct bt_uuid *bleio_uuid_as_bt_uuid(bleio_uuid_obj_t *self);
