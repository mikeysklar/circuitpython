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

typedef struct _bleio_characteristic_obj bleio_characteristic_obj_t;

typedef void *ble_event_handler_t;

typedef struct {
    mp_obj_base_t base;
    bleio_characteristic_obj_t *characteristic;
    // Incoming packets are length-prefixed in this ringbuf so packet
    // boundaries survive: a plain byte stream would lose framing, which is the
    // whole point of PacketBuffer over CharacteristicBuffer.
    ringbuf_t incoming;
    uint16_t max_packet_size;
    bool deinited;
} bleio_packet_buffer_obj_t;

struct bt_conn;

// Called from the ATT write callback (Bluetooth RX thread context).
void bleio_packet_buffer_extend(bleio_packet_buffer_obj_t *self,
    struct bt_conn *conn, const void *buf, uint16_t len);
