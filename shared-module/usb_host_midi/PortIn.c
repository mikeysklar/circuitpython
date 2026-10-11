// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Mikey Sklar for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <string.h>

#include "shared-bindings/usb_host_midi/PortIn.h"
#include "shared-module/usb_host_midi/PortIn.h"
#include "py/mperrno.h"
#include "py/stream.h"
#include "supervisor/usb.h"
#include "tusb.h"

#define DEINITED_IDX (0xff)

static bool device_gone(usb_host_midi_portin_obj_t *self) {
    return self->generation != usb_host_midi_generation() || !tuh_midi_mounted(self->idx);
}

size_t common_hal_usb_host_midi_portin_read(usb_host_midi_portin_obj_t *self, uint8_t *data, size_t len, int *errcode) {
    if (device_gone(self)) {
        *errcode = MP_ENODEV;
        return MP_STREAM_ERROR;
    }
    size_t n = 0;
    while (n < len) {
        if (self->pending_len == 0) {
            // Leave 2 bytes of slack because tuh_midi_stream_read() can write
            // a whole 3 byte message after checking only that one byte fits.
            // It also stops at a cable change, so the next pass reads on.
            uint8_t cable;
            self->pending_start = 0;
            self->pending_len = tuh_midi_stream_read(self->idx, &cable, self->pending, sizeof(self->pending) - 2);
            if (self->pending_len == 0) {
                break;
            }
        }
        size_t count = MIN(self->pending_len, len - n);
        memcpy(data + n, self->pending + self->pending_start, count);
        self->pending_start += count;
        self->pending_len -= count;
        n += count;
    }
    return n;
}

uint32_t common_hal_usb_host_midi_portin_bytes_available(usb_host_midi_portin_obj_t *self) {
    if (device_gone(self)) {
        return 0;
    }
    // Counts raw 4 byte packets in the driver FIFO, so it is only a nonzero
    // check, not an exact byte count.
    return self->pending_len + tuh_midi_read_available(self->idx);
}

void common_hal_usb_host_midi_portin_deinit(usb_host_midi_portin_obj_t *self) {
    self->idx = DEINITED_IDX;
}

bool common_hal_usb_host_midi_portin_deinited(usb_host_midi_portin_obj_t *self) {
    return self->idx == DEINITED_IDX;
}
