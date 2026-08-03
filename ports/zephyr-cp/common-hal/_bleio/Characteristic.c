// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <string.h>

#include "py/runtime.h"
#include "shared-bindings/_bleio/Characteristic.h"
#include "shared-bindings/_bleio/Descriptor.h"
#include "shared-bindings/_bleio/Service.h"
#include "shared-bindings/_bleio/CharacteristicBuffer.h"
#include "shared-bindings/_bleio/PacketBuffer.h"
#include "bindings/zephyr_kernel/__init__.h"

#include <zephyr/bluetooth/gatt.h>

bleio_characteristic_properties_t common_hal_bleio_characteristic_get_properties(bleio_characteristic_obj_t *self) {
    return self->props;
}

mp_obj_tuple_t *common_hal_bleio_characteristic_get_descriptors(bleio_characteristic_obj_t *self) {
    return mp_obj_new_tuple(self->descriptor_list->len, self->descriptor_list->items);
}

bleio_service_obj_t *common_hal_bleio_characteristic_get_service(bleio_characteristic_obj_t *self) {
    return self->service;
}

bleio_uuid_obj_t *common_hal_bleio_characteristic_get_uuid(bleio_characteristic_obj_t *self) {
    return self->uuid;
}

size_t common_hal_bleio_characteristic_get_max_length(bleio_characteristic_obj_t *self) {
    return self->max_length;
}

size_t common_hal_bleio_characteristic_get_value(bleio_characteristic_obj_t *self, uint8_t *buf, size_t len) {
    // Local GATT server value. Reading a REMOTE characteristic would need
    // central-role discovery, which this port does not implement.
    size_t n = self->current_value_len;
    if (n > len) {
        n = len;
    }
    if (self->current_value != NULL && n > 0) {
        memcpy(buf, self->current_value, n);
    }
    return n;
}

void common_hal_bleio_characteristic_add_descriptor(bleio_characteristic_obj_t *self, bleio_descriptor_obj_t *descriptor) {
    // Extra descriptors beyond the auto-generated CCCD are not supported: the
    // attribute table is sized per characteristic in Service.h.
    mp_raise_NotImplementedError(NULL);
}

void common_hal_bleio_characteristic_construct(bleio_characteristic_obj_t *self, bleio_service_obj_t *service, uint16_t handle, bleio_uuid_obj_t *uuid, bleio_characteristic_properties_t props, bleio_attribute_security_mode_t read_perm, bleio_attribute_security_mode_t write_perm, mp_int_t max_length, bool fixed_length, mp_buffer_info_t *initial_value_bufinfo, const char *user_description) {
    if (max_length < 0) {
        mp_raise_ValueError(MP_ERROR_TEXT("Invalid data_length"));
    }
    self->service = service;
    self->uuid = uuid;
    self->handle = handle;
    self->props = props;
    self->read_perm = read_perm;
    self->write_perm = write_perm;
    self->max_length = (uint16_t)max_length;
    self->fixed_length = fixed_length;
    self->observer = mp_const_none;
    self->descriptor_list = mp_obj_new_list(0, NULL);
    self->current_value = NULL;
    self->current_value_len = 0;
    self->current_value_alloc = 0;
    self->value_attr = NULL;
    self->cccd_attr = NULL;
    memset(&self->chrc, 0, sizeof(self->chrc));

    if (initial_value_bufinfo != NULL && initial_value_bufinfo->len > 0) {
        if (initial_value_bufinfo->len > self->max_length) {
            mp_raise_ValueError(MP_ERROR_TEXT("Value length != required fixed length"));
        }
        self->current_value = m_malloc(self->max_length);
        self->current_value_alloc = self->max_length;
        memcpy(self->current_value, initial_value_bufinfo->buf, initial_value_bufinfo->len);
        self->current_value_len = initial_value_bufinfo->len;
    }

    // Attach to the service's attribute table. EVERY other port does this from
    // here (nordic Characteristic.c:108, espressif:110, silabs:145) -- omitting
    // it is a SILENT WRONG SUCCESS: the characteristic object works standalone
    // (set_value and readback both pass) but never appears in
    // service.characteristics and gets no GATT attributes, so a connected
    // central cannot see it. The symptom is `len(svc.characteristics) == 0`
    // while every individual operation reports OK.
    if (service->is_remote) {
        self->handle = handle;
    } else {
        common_hal_bleio_service_add_characteristic(self->service, self,
            initial_value_bufinfo, user_description);
    }
}

bool common_hal_bleio_characteristic_deinited(bleio_characteristic_obj_t *self) {
    return self->service == NULL;
}

void common_hal_bleio_characteristic_deinit(bleio_characteristic_obj_t *self) {
    // Nothing to do
}

void common_hal_bleio_characteristic_set_cccd(bleio_characteristic_obj_t *self, bool notify, bool indicate) {
    // Writing a CCCD is a CENTRAL-role operation (subscribing to a remote
    // peripheral). As a peripheral the CCCD is written by the connected
    // central and handled by bt_gatt_attr_write_ccc.
    mp_raise_NotImplementedError(NULL);
}

void common_hal_bleio_characteristic_set_value(bleio_characteristic_obj_t *self, mp_buffer_info_t *bufinfo) {
    if (self->fixed_length && bufinfo->len != self->max_length) {
        mp_raise_ValueError(MP_ERROR_TEXT("Value length != required fixed length"));
    }
    if (bufinfo->len > self->max_length) {
        mp_raise_ValueError(MP_ERROR_TEXT("Value length > max_length"));
    }
    if (self->current_value == NULL) {
        self->current_value = m_malloc(self->max_length);
        self->current_value_alloc = self->max_length;
    }
    memcpy(self->current_value, bufinfo->buf, bufinfo->len);
    self->current_value_len = bufinfo->len;

    // Push to any subscribed central. With no connection or no subscriber the
    // stack legitimately refuses: -ENOTCONN (no link), -EINVAL (attr not yet
    // registered) and -ENOENT (registered, but nobody has written the CCCD to
    // subscribe) are all NORMAL states for a peripheral sitting idle, not
    // errors to raise. Before the characteristic was attached to the service
    // this path returned -EINVAL; attaching it made -ENOENT the common case.
    if (self->value_attr != NULL &&
        (self->props & (CHAR_PROP_NOTIFY | CHAR_PROP_INDICATE))) {
        int err = bt_gatt_notify(NULL, self->value_attr,
            self->current_value, self->current_value_len);
        if (err != 0 && err != -ENOTCONN && err != -EINVAL && err != -ENOENT) {
            raise_zephyr_error(err);
        }
    }
}

void bleio_characteristic_set_observer(bleio_characteristic_obj_t *self, mp_obj_t observer) {
    self->observer = observer;
}

void bleio_characteristic_clear_observer(bleio_characteristic_obj_t *self) {
    self->observer = mp_const_none;
}

// --- Zephyr ATT callbacks ---------------------------------------------------
// These run in the Bluetooth RX thread, NOT on the CircuitPython VM thread, so
// they must not allocate on the GC heap or raise MicroPython exceptions.

ssize_t bleio_characteristic_attr_read(struct bt_conn *conn,
    const struct bt_gatt_attr *attr, void *buf, uint16_t len, uint16_t offset) {
    bleio_characteristic_obj_t *self = attr->user_data;
    printk("bleio: ATT read handle %u offset %u len %u\n", attr->handle, offset, len);
    if (self->current_value == NULL) {
        return bt_gatt_attr_read(conn, attr, buf, len, offset, NULL, 0);
    }
    return bt_gatt_attr_read(conn, attr, buf, len, offset,
        self->current_value, self->current_value_len);
}

ssize_t bleio_characteristic_attr_write(struct bt_conn *conn,
    const struct bt_gatt_attr *attr, const void *buf, uint16_t len,
    uint16_t offset, uint8_t flags) {
    (void)conn;
    (void)flags;
    bleio_characteristic_obj_t *self = attr->user_data;
    printk("bleio: ATT write handle %u offset %u len %u\n", attr->handle, offset, len);

    if (offset + len > self->max_length) {
        return BT_GATT_ERR(BT_ATT_ERR_INVALID_ATTRIBUTE_LEN);
    }
    if (self->current_value == NULL) {
        return BT_GATT_ERR(BT_ATT_ERR_WRITE_NOT_PERMITTED);
    }
    memcpy(self->current_value + offset, buf, len);
    if (offset + len > self->current_value_len) {
        self->current_value_len = offset + len;
    }

    // Feed an attached CharacteristicBuffer/PacketBuffer observer so Python
    // code can see incoming writes. The observer is a plain C-side struct
    // pointer; no allocation happens here.
    if (self->observer != mp_const_none && self->observer != MP_OBJ_NULL) {
        const mp_obj_type_t *t = mp_obj_get_type(self->observer);
        if (t == &bleio_characteristic_buffer_type) {
            bleio_characteristic_buffer_extend(
                MP_OBJ_TO_PTR(self->observer), buf, len);
        } else if (t == &bleio_packet_buffer_type) {
            bleio_packet_buffer_extend(
                MP_OBJ_TO_PTR(self->observer), conn, buf, len);
        }
    }
    return len;
}

void bleio_characteristic_ccc_changed(const struct bt_gatt_attr *attr, uint16_t value) {
    (void)attr;
    (void)value;
    // Subscription state is tracked by Zephyr; bt_gatt_notify(NULL, ...) is a
    // no-op when nobody is subscribed, so nothing is needed here.
}
