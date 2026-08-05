// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include <stdbool.h>

#include "py/obj.h"
#include "py/ringbuf.h"
#include "shared-bindings/_bleio/Characteristic.h"

typedef struct {
    mp_obj_base_t base;
    bleio_characteristic_obj_t *characteristic;
    mp_float_t timeout;
    // Written from the Bluetooth RX thread, drained from the VM thread.
    ringbuf_t ringbuf;
    bool watch_for_interrupt_char;
    bool deinited;
} bleio_characteristic_buffer_obj_t;

// Called from the ATT write callback (Bluetooth RX thread context).
void bleio_characteristic_buffer_extend(bleio_characteristic_buffer_obj_t *self,
    const void *buf, uint16_t len);
