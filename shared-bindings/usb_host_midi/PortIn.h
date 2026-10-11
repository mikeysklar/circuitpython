// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Mikey Sklar for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "shared-module/usb_host_midi/PortIn.h"

extern const mp_obj_type_t usb_host_midi_portin_type;

// Returns the number of bytes copied into data, 0 if nothing is buffered.
// Returns MP_STREAM_ERROR and sets *errcode to MP_ENODEV if the device was
// unplugged since the port was opened.
size_t common_hal_usb_host_midi_portin_read(usb_host_midi_portin_obj_t *self,
    uint8_t *data, size_t len, int *errcode);

// Nonzero when a read would return data. Not an exact byte count.
uint32_t common_hal_usb_host_midi_portin_bytes_available(usb_host_midi_portin_obj_t *self);

void common_hal_usb_host_midi_portin_deinit(usb_host_midi_portin_obj_t *self);
bool common_hal_usb_host_midi_portin_deinited(usb_host_midi_portin_obj_t *self);
