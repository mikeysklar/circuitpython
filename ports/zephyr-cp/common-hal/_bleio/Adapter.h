// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2018 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"
#include "py/objtuple.h"

#include "shared-bindings/_bleio/Connection.h"
#include "shared-bindings/_bleio/ScanResults.h"

#define BLEIO_TOTAL_CONNECTION_COUNT CONFIG_BT_MAX_CONN

extern bleio_connection_internal_t bleio_connections[BLEIO_TOTAL_CONNECTION_COUNT];

typedef struct {
    mp_obj_base_t base;
    bleio_scanresults_obj_t *scan_results;
    mp_obj_t name;
    mp_obj_tuple_t *connection_objs;
    bool user_advertising;
} bleio_adapter_obj_t;

void bleio_adapter_gc_collect(bleio_adapter_obj_t *adapter);
void bleio_adapter_reset(bleio_adapter_obj_t *adapter);

// Deferred BLE work that must not run in Zephyr's BT thread.  Called from
// port_background_task().
void bleio_background(void);

typedef struct bleio_service_obj bleio_service_obj_t;

// Queues a Service for bt_gatt_service_register(), which is deferred until
// advertising starts (see common_hal_bleio_adapter_start_advertising()) so
// that a service built up with several add_characteristic() calls registers
// once, fully formed, rather than being re-registered after every call.
void bleio_adapter_add_pending_service(bleio_service_obj_t *self);

bool bleio_adapter_any_connected(void);
