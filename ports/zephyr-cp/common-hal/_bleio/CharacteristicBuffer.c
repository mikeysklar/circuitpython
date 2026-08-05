// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <string.h>

#include "py/mperrno.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "shared-bindings/_bleio/CharacteristicBuffer.h"
#include "shared/runtime/interrupt_char.h"
#include "supervisor/shared/tick.h"

#include <zephyr/kernel.h>

// The ringbuf is filled from the Bluetooth RX thread and drained from the VM
// thread, so every access is wrapped in an irq lock. Zephyr's k_sched_lock is
// not enough: the BT RX thread can be on another priority and the ringbuf
// head/tail updates are not atomic.
static inline unsigned int buf_lock(void) {
    return irq_lock();
}

static inline void buf_unlock(unsigned int key) {
    irq_unlock(key);
}

void _common_hal_bleio_characteristic_buffer_construct(bleio_characteristic_buffer_obj_t *self,
    bleio_characteristic_obj_t *characteristic,
    mp_float_t timeout,
    uint8_t *buffer, size_t buffer_size,
    void *static_handler_entry,
    bool watch_for_interrupt_char) {
    (void)static_handler_entry;

    self->characteristic = characteristic;
    self->timeout = timeout;
    self->watch_for_interrupt_char = watch_for_interrupt_char;
    self->deinited = false;
    ringbuf_init(&self->ringbuf, buffer, buffer_size);

    // Route incoming ATT writes for this characteristic to this buffer.
    bleio_characteristic_set_observer(characteristic, MP_OBJ_FROM_PTR(self));
}

void common_hal_bleio_characteristic_buffer_construct(bleio_characteristic_buffer_obj_t *self,
    bleio_characteristic_obj_t *characteristic,
    mp_float_t timeout,
    size_t buffer_size) {
    uint8_t *buffer = m_malloc(buffer_size);
    _common_hal_bleio_characteristic_buffer_construct(self, characteristic,
        timeout, buffer, buffer_size, NULL, false);
}

void bleio_characteristic_buffer_extend(bleio_characteristic_buffer_obj_t *self,
    const void *buf, uint16_t len) {
    if (self == NULL || self->deinited) {
        return;
    }
    const uint8_t *bytes = buf;
    unsigned int key = buf_lock();
    for (uint16_t i = 0; i < len; i++) {
        if (self->watch_for_interrupt_char && bytes[i] == mp_interrupt_char) {
            // Deliver the KeyboardInterrupt via the normal supervisor path
            // rather than buffering the character.
            buf_unlock(key);
            mp_sched_keyboard_interrupt();
            key = buf_lock();
            continue;
        }
        // ringbuf_put returns -1 when full: oldest-wins would corrupt framing,
        // so drop the newest byte instead, matching other ports.
        if (ringbuf_put(&self->ringbuf, bytes[i]) < 0) {
            break;
        }
    }
    buf_unlock(key);
}

uint32_t common_hal_bleio_characteristic_buffer_read(bleio_characteristic_buffer_obj_t *self, uint8_t *data, size_t len, int *errcode) {
    if (self->deinited) {
        if (errcode != NULL) {
            *errcode = MP_EINVAL;
        }
        return 0;
    }

    uint64_t start_ticks = supervisor_ticks_ms64();
    uint32_t timeout_ms = (uint32_t)(self->timeout * 1000.0f);

    // Block until at least one byte is available or the timeout expires. A
    // zero timeout means non-blocking.
    while (true) {
        unsigned int key = buf_lock();
        int avail = ringbuf_num_filled(&self->ringbuf);
        buf_unlock(key);
        if (avail > 0) {
            break;
        }
        if (timeout_ms == 0 ||
            (supervisor_ticks_ms64() - start_ticks) >= timeout_ms) {
            return 0;
        }
        RUN_BACKGROUND_TASKS;
        if (mp_hal_is_interrupted()) {
            return 0;
        }
    }

    uint32_t n = 0;
    unsigned int key = buf_lock();
    while (n < len) {
        int b = ringbuf_get(&self->ringbuf);
        if (b < 0) {
            break;
        }
        data[n++] = (uint8_t)b;
    }
    buf_unlock(key);
    return n;
}

uint32_t common_hal_bleio_characteristic_buffer_rx_characters_available(bleio_characteristic_buffer_obj_t *self) {
    if (self->deinited) {
        return 0;
    }
    unsigned int key = buf_lock();
    int n = ringbuf_num_filled(&self->ringbuf);
    buf_unlock(key);
    return n < 0 ? 0 : (uint32_t)n;
}

void common_hal_bleio_characteristic_buffer_clear_rx_buffer(bleio_characteristic_buffer_obj_t *self) {
    if (self->deinited) {
        return;
    }
    unsigned int key = buf_lock();
    ringbuf_clear(&self->ringbuf);
    buf_unlock(key);
}

bool common_hal_bleio_characteristic_buffer_deinited(bleio_characteristic_buffer_obj_t *self) {
    return self->deinited;
}

void common_hal_bleio_characteristic_buffer_deinit(bleio_characteristic_buffer_obj_t *self) {
    if (self == NULL || self->deinited) {
        return;
    }
    if (self->characteristic != NULL) {
        bleio_characteristic_clear_observer(self->characteristic);
    }
    self->deinited = true;
}

bool common_hal_bleio_characteristic_buffer_connected(bleio_characteristic_buffer_obj_t *self) {
    // A GATT server characteristic is writable only while a central is
    // connected, so reuse the adapter's connection state.
    extern bool bleio_adapter_any_connected(void);
    return !self->deinited && bleio_adapter_any_connected();
}
