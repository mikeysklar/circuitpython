// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Mikey Sklar for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include <stdint.h>

#include "py/obj.h"

typedef struct {
    mp_obj_base_t base;
    uint32_t generation;  // usb_host_midi_generation() when opened
    uint8_t idx;          // TinyUSB MIDI index, 0xFF once deinited
    // tuh_midi_stream_read() can write up to 2 bytes past the size it is
    // given, so it fills this with 2 bytes of slack. Bytes the caller did not
    // ask for yet stay here for the next read.
    uint8_t pending_start;
    uint8_t pending_len;
    uint8_t pending[64];
} usb_host_midi_portin_obj_t;
