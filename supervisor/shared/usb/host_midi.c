// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Mikey Sklar for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "tusb.h"

#include "supervisor/usb.h"

// TinyUSB's MIDI host driver tracks the device address, interface and cable
// counts itself. We only count mounts and unmounts so that an open port can
// tell when the device it was opened on has gone away. On ESP32 the callbacks
// run in the TinyUSB task, so the counter is volatile.
static volatile uint32_t _generation;

void tuh_midi_mount_cb(uint8_t idx, const tuh_midi_mount_cb_t *mount_cb_data) {
    (void)idx;
    (void)mount_cb_data;
    _generation++;
}

void tuh_midi_umount_cb(uint8_t idx) {
    (void)idx;
    _generation++;
}

uint32_t usb_host_midi_generation(void) {
    return _generation;
}
