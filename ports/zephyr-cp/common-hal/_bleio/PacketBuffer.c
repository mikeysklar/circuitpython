// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <string.h>

#include "py/mperrno.h"
#include "py/runtime.h"
#include "shared-bindings/_bleio/PacketBuffer.h"
#include "shared-bindings/_bleio/Characteristic.h"

#include <zephyr/kernel.h>
#include <zephyr/bluetooth/gatt.h>

// Packets are stored length-prefixed (2-byte little-endian length followed by
// the payload) so that packet boundaries survive the ring buffer. Reading a
// PacketBuffer must return exactly one packet per call.

static inline unsigned int buf_lock(void) {
    return irq_lock();
}

static inline void buf_unlock(unsigned int key) {
    irq_unlock(key);
}

void _common_hal_bleio_packet_buffer_construct(
    bleio_packet_buffer_obj_t *self, bleio_characteristic_obj_t *characteristic,
    uint32_t *incoming_buffer, size_t incoming_buffer_size,
    uint32_t *outgoing_buffer1, uint32_t *outgoing_buffer2, size_t max_packet_size,
    ble_event_handler_t *static_handler_entry) {
    (void)outgoing_buffer1;
    (void)outgoing_buffer2;
    (void)static_handler_entry;

    self->characteristic = characteristic;
    self->max_packet_size = (uint16_t)max_packet_size;
    self->deinited = false;
    ringbuf_init(&self->incoming, (uint8_t *)incoming_buffer, incoming_buffer_size);

    bleio_characteristic_set_observer(characteristic, MP_OBJ_FROM_PTR(self));
}

void common_hal_bleio_packet_buffer_construct(
    bleio_packet_buffer_obj_t *self, bleio_characteristic_obj_t *characteristic,
    size_t buffer_size, size_t max_packet_size) {
    if (max_packet_size == 0) {
        max_packet_size = characteristic->max_length;
    }
    // Room for buffer_size packets of max_packet_size plus their 2-byte length
    // prefixes.
    size_t bytes = (max_packet_size + 2) * (buffer_size ? buffer_size : 1);
    uint8_t *storage = m_malloc(bytes);
    _common_hal_bleio_packet_buffer_construct(self, characteristic,
        (uint32_t *)storage, bytes, NULL, NULL, max_packet_size, NULL);
}

void bleio_packet_buffer_extend(bleio_packet_buffer_obj_t *self,
    struct bt_conn *conn, const void *buf, uint16_t len) {
    (void)conn;
    if (self == NULL || self->deinited || len == 0) {
        return;
    }
    const uint8_t *bytes = buf;
    unsigned int key = buf_lock();
    // Drop the whole packet if it does not fit: a partial packet would
    // desynchronise every subsequent read.
    if (ringbuf_num_empty(&self->incoming) >= (int)len + 2) {
        ringbuf_put(&self->incoming, len & 0xff);
        ringbuf_put(&self->incoming, (len >> 8) & 0xff);
        for (uint16_t i = 0; i < len; i++) {
            ringbuf_put(&self->incoming, bytes[i]);
        }
    }
    buf_unlock(key);
}

mp_int_t common_hal_bleio_packet_buffer_write(bleio_packet_buffer_obj_t *self, const uint8_t *data, size_t len, uint8_t *header, size_t header_len) {
    if (self->deinited || self->characteristic == NULL) {
        return -MP_EINVAL;
    }
    if (len + header_len > self->max_packet_size) {
        return -MP_EINVAL;
    }

    uint8_t packet[BLEIO_PACKET_BUFFER_MAX_PACKET_SIZE];
    size_t total = 0;
    if (header_len > 0 && header != NULL) {
        memcpy(packet, header, header_len);
        total += header_len;
    }
    memcpy(packet + total, data, len);
    total += len;

    bleio_characteristic_obj_t *chr = self->characteristic;
    if (chr->value_attr == NULL) {
        return -MP_ENOTCONN;
    }
    // Notify rather than storing: PacketBuffer is a stream abstraction, so each
    // write is an outgoing packet to the subscribed central.
    //
    // NOTE (adapted from the reference, not independently verified): this
    // treats -ENOENT as a hard error (falls through to -MP_EIO below) while
    // Characteristic.c's set_value treats -ENOENT as a normal idle state
    // alongside -ENOTCONN/-EINVAL. Neither path has been exercised by a real
    // subscriber, so which behavior is actually correct is unresolved -- see
    // PR discussion. Left as-is pending a real notify test.
    int err = bt_gatt_notify(NULL, chr->value_attr, packet, total);
    if (err == -ENOTCONN || err == -EINVAL) {
        return -MP_ENOTCONN;
    }
    if (err != 0) {
        return -MP_EIO;
    }
    return (mp_int_t)total;
}

mp_int_t common_hal_bleio_packet_buffer_readinto(bleio_packet_buffer_obj_t *self, uint8_t *data, size_t len) {
    if (self->deinited) {
        return -MP_EINVAL;
    }
    unsigned int key = buf_lock();
    if (ringbuf_num_filled(&self->incoming) < 2) {
        buf_unlock(key);
        return 0;
    }
    int lo = ringbuf_get(&self->incoming);
    int hi = ringbuf_get(&self->incoming);
    uint16_t plen = (uint16_t)((hi << 8) | lo);
    if (plen > len) {
        // Caller's buffer is too small. Consume the packet anyway so the
        // stream stays framed, and report the required size.
        for (uint16_t i = 0; i < plen; i++) {
            ringbuf_get(&self->incoming);
        }
        buf_unlock(key);
        return -MP_EINVAL;
    }
    for (uint16_t i = 0; i < plen; i++) {
        int b = ringbuf_get(&self->incoming);
        data[i] = (uint8_t)(b < 0 ? 0 : b);
    }
    buf_unlock(key);
    return (mp_int_t)plen;
}

mp_int_t common_hal_bleio_packet_buffer_get_incoming_packet_length(bleio_packet_buffer_obj_t *self) {
    if (self->deinited) {
        return -1;
    }
    return self->max_packet_size;
}

mp_int_t common_hal_bleio_packet_buffer_get_outgoing_packet_length(bleio_packet_buffer_obj_t *self) {
    if (self->deinited) {
        return -1;
    }
    return self->max_packet_size;
}

void common_hal_bleio_packet_buffer_flush(bleio_packet_buffer_obj_t *self) {
    // Writes go out synchronously via bt_gatt_notify, so there is nothing
    // queued to flush.
}

bool common_hal_bleio_packet_buffer_deinited(bleio_packet_buffer_obj_t *self) {
    return self->deinited;
}

void common_hal_bleio_packet_buffer_deinit(bleio_packet_buffer_obj_t *self) {
    if (self == NULL || self->deinited) {
        return;
    }
    if (self->characteristic != NULL) {
        bleio_characteristic_clear_observer(self->characteristic);
    }
    self->deinited = true;
}

bool common_hal_bleio_packet_buffer_connected(bleio_packet_buffer_obj_t *self) {
    extern bool bleio_adapter_any_connected(void);
    return !self->deinited && bleio_adapter_any_connected();
}
