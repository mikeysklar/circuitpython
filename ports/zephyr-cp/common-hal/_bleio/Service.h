// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"
#include "py/objlist.h"
#include "common-hal/_bleio/UUID.h"

#include <zephyr/bluetooth/gatt.h>

// Attribute-table budget for ONE service. Each characteristic costs 3 slots in
// the worst case (declaration + value + CCCD), plus 1 for the primary service
// declaration itself.
#define BLEIO_SERVICE_MAX_CHARACTERISTICS 8
#define BLEIO_SERVICE_MAX_ATTRS (1 + (BLEIO_SERVICE_MAX_CHARACTERISTICS * 3))

typedef struct bleio_service_obj {
    mp_obj_base_t base;
    bleio_uuid_obj_t *uuid;
    mp_obj_t connection;
    mp_obj_list_t *characteristic_list;
    uint16_t start_handle;
    uint16_t end_handle;
    bool is_remote;
    bool is_secondary;

    // Zephyr GATT server state. The attr array and the _ccc storage must
    // remain valid for as long as the service is registered -- Zephyr keeps
    // pointers into both -- so they are embedded here rather than allocated
    // per registration.
    struct bt_gatt_attr attrs[BLEIO_SERVICE_MAX_ATTRS];
    struct _bt_gatt_ccc cccs[BLEIO_SERVICE_MAX_CHARACTERISTICS];
    struct bt_gatt_service registration;
    uint16_t attr_count;
    bool registered;
} bleio_service_obj_t;

// Register (or re-register) the service's attribute table with Zephyr.
// Called when advertising starts, once all characteristics have been added.
void bleio_service_register_if_needed(bleio_service_obj_t *self);
