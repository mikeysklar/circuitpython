// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2019 Dan Halbert for Adafruit Industries
// SPDX-FileCopyrightText: Copyright (c) 2026 Scott Shawcroft for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <errno.h>
#include <string.h>

#include "py/runtime.h"
#include "shared-bindings/_bleio/Service.h"
#include "shared-bindings/_bleio/Characteristic.h"
#include "bindings/zephyr_kernel/__init__.h"
#include "common-hal/_bleio/Adapter.h"

#include <zephyr/bluetooth/gatt.h>
#include <zephyr/bluetooth/uuid.h>

// PERSISTENT declaration UUIDs.
//
// BT_UUID_GATT_PRIMARY / _SECONDARY / _CHRC / _CCC expand to
// BT_UUID_DECLARE_16, which is a COMPOUND LITERAL:
//
//   #define BT_UUID_DECLARE_16(value) \
//       ((const struct bt_uuid *) ((const struct bt_uuid_16[]) {BT_UUID_INIT_16(value)}))
//
// At FILE scope a compound literal has static storage duration; inside a
// function it has AUTOMATIC storage and dies with the stack frame. Zephyr's
// own BT_GATT_SERVICE_DEFINE only uses these macros at file scope, so the
// hazard never shows up upstream. Storing one into a heap-allocated
// bt_gatt_attr from inside a function leaves the attribute table pointing at
// dead stack: bt_gatt_service_register() still returns 0 because it validates
// the pointers without dereferencing them, then every later ATT request reads
// garbage. Observed bt_uuid->type values of 1, 158, 158 where only 0/1/2 are
// legal -- and one entry intact by luck, which makes it look intermittent.
//
// No compiler diagnostic catches this: -Wdangling-pointer=2,
// -Wreturn-local-addr, -Wuse-after-free=3 and -fanalyzer are all silent,
// because the pointer is stored THROUGH a pointer into a struct field rather
// than returned.
//
// Upstream's convention for a UUID that must outlive its block is the INIT
// form with static storage (subsys/bluetooth/host/gatt.c:5363,
// audio/csip_set_coordinator.c:92, audio/mcc.c:60). Match it.
static const struct bt_uuid_16 bleio_uuid_primary = BT_UUID_INIT_16(BT_UUID_GATT_PRIMARY_VAL);
static const struct bt_uuid_16 bleio_uuid_secondary = BT_UUID_INIT_16(BT_UUID_GATT_SECONDARY_VAL);
static const struct bt_uuid_16 bleio_uuid_chrc = BT_UUID_INIT_16(BT_UUID_GATT_CHRC_VAL);
static const struct bt_uuid_16 bleio_uuid_ccc = BT_UUID_INIT_16(BT_UUID_GATT_CCC_VAL);

// Zephyr's GATT server is table driven: a service is an array of bt_gatt_attr
// registered with bt_gatt_service_register(). Nothing in this port previously
// referenced bt_gatt_service_register or BT_GATT_SERVICE_DEFINE at all -- the
// GATT server simply did not exist -- so this file builds the table at runtime
// as characteristics are added from Python.
//
// BT_GATT_SERVICE_DEFINE is deliberately NOT used: it is a static-initialiser
// macro that requires the whole service to be known at compile time, and
// CircuitPython builds services dynamically.

uint32_t _common_hal_bleio_service_construct(bleio_service_obj_t *self, bleio_uuid_obj_t *uuid, bool is_secondary, mp_obj_list_t *characteristic_list) {
    self->uuid = uuid;
    // The INTERNAL constructor takes the list as a parameter (the public one
    // allocates it and delegates here). The BLE workflow uses this path, and a
    // NULL list here is a latent HARD FAULT rather than an error: both
    // get_characteristics() and add_characteristic() dereference
    // characteristic_list->len unconditionally. Allocate a fallback so the
    // workflow path cannot crash the board.
    if (characteristic_list == NULL) {
        characteristic_list = mp_obj_new_list(0, NULL);
    }
    self->characteristic_list = characteristic_list;
    self->is_remote = false;
    self->is_secondary = is_secondary;
    self->connection = MP_OBJ_NULL;
    self->start_handle = 0;
    self->end_handle = 0;
    self->attr_count = 0;
    self->registered = false;
    memset(self->attrs, 0, sizeof(self->attrs));
    memset(self->cccs, 0, sizeof(self->cccs));

    // Slot 0 is the primary/secondary service declaration. Its user_data is
    // the service UUID, which must live in the UUID object (not the stack).
    const struct bt_uuid *svc_uuid = bleio_uuid_as_bt_uuid(uuid);
    self->attrs[0].uuid = is_secondary ? &bleio_uuid_secondary.uuid : &bleio_uuid_primary.uuid;
    self->attrs[0].perm = BT_GATT_PERM_READ;
    self->attrs[0].read = bt_gatt_attr_read_service;
    self->attrs[0].user_data = (void *)svc_uuid;
    self->attr_count = 1;

    // Queue for registration when advertising starts.
    bleio_adapter_add_pending_service(self);

    return 0;
}

void common_hal_bleio_service_construct(bleio_service_obj_t *self, bleio_uuid_obj_t *uuid, bool is_secondary) {
    _common_hal_bleio_service_construct(self, uuid, is_secondary,
        mp_obj_new_list(0, NULL));
}

void common_hal_bleio_service_deinit(bleio_service_obj_t *self) {
    if (self->registered) {
        bt_gatt_service_unregister(&self->registration);
        self->registered = false;
    }
}

void common_hal_bleio_service_from_remote_service(bleio_service_obj_t *self, bleio_connection_obj_t *connection, bleio_uuid_obj_t *uuid, bool is_secondary) {
    // Central-role service discovery is not implemented; only the peripheral
    // GATT server is. Raising here is accurate rather than silently returning
    // an empty service.
    mp_raise_NotImplementedError(NULL);
}

bleio_uuid_obj_t *common_hal_bleio_service_get_uuid(bleio_service_obj_t *self) {
    return self->uuid;
}

mp_obj_tuple_t *common_hal_bleio_service_get_characteristics(bleio_service_obj_t *self) {
    return mp_obj_new_tuple(self->characteristic_list->len, self->characteristic_list->items);
}

bool common_hal_bleio_service_get_is_remote(bleio_service_obj_t *self) {
    return self->is_remote;
}

bool common_hal_bleio_service_get_is_secondary(bleio_service_obj_t *self) {
    return self->is_secondary;
}

void bleio_service_register_if_needed(bleio_service_obj_t *self) {
    if (self->registered || self->attr_count == 0) {
        return;
    }
    self->registration.attrs = self->attrs;
    self->registration.attr_count = self->attr_count;
    int err = bt_gatt_service_register(&self->registration);
    if (err != 0) {
        raise_zephyr_error(err);
    }
    self->registered = true;
    // Handles are assigned by the stack during registration.
    self->start_handle = self->attrs[0].handle;
    self->end_handle = self->attrs[self->attr_count - 1].handle;
    printk("bleio: service registered, %u attrs, handles %u-%u\n",
        (unsigned)self->attr_count, self->start_handle, self->end_handle);
}

void common_hal_bleio_service_add_characteristic(bleio_service_obj_t *self, bleio_characteristic_obj_t *characteristic, mp_buffer_info_t *initial_value_bufinfo, const char *user_description) {
    (void)user_description;

    if (self->registered) {
        // Zephyr assigns handles at registration; the table cannot grow after.
        raise_zephyr_error(-EALREADY);
    }

    size_t chr_index = self->characteristic_list->len;
    if (chr_index >= BLEIO_SERVICE_MAX_CHARACTERISTICS ||
        self->attr_count + 3 > BLEIO_SERVICE_MAX_ATTRS) {
        raise_zephyr_error(-ENOSPC);
    }

    const struct bt_uuid *chr_uuid = bleio_uuid_as_bt_uuid(characteristic->uuid);

    // Translate CircuitPython properties to Zephyr's characteristic props and
    // attribute permissions.
    uint8_t props = 0;
    uint8_t perm = 0;
    bleio_characteristic_properties_t p = characteristic->props;
    if (p & CHAR_PROP_READ) {
        props |= BT_GATT_CHRC_READ;
        perm |= BT_GATT_PERM_READ;
    }
    if (p & CHAR_PROP_WRITE) {
        props |= BT_GATT_CHRC_WRITE;
        perm |= BT_GATT_PERM_WRITE;
    }
    if (p & CHAR_PROP_WRITE_NO_RESPONSE) {
        props |= BT_GATT_CHRC_WRITE_WITHOUT_RESP;
        perm |= BT_GATT_PERM_WRITE;
    }
    if (p & CHAR_PROP_NOTIFY) {
        props |= BT_GATT_CHRC_NOTIFY;
    }
    if (p & CHAR_PROP_INDICATE) {
        props |= BT_GATT_CHRC_INDICATE;
    }

    // The value buffer is already allocated and seeded by
    // common_hal_bleio_characteristic_construct, which is the caller. Doing it
    // again here would leak the first allocation and reset current_value_len.
    if (characteristic->current_value == NULL) {
        characteristic->current_value = m_malloc(characteristic->max_length);
        characteristic->current_value_alloc = characteristic->max_length;
        characteristic->current_value_len = 0;
    }

    // The bt_gatt_chrc user_data for the declaration attribute must persist,
    // so it is stored inside the characteristic object.
    characteristic->chrc.uuid = chr_uuid;
    characteristic->chrc.properties = props;
    characteristic->chrc.value_handle = 0;

    // Attribute 1: characteristic declaration.
    struct bt_gatt_attr *decl = &self->attrs[self->attr_count++];
    decl->uuid = &bleio_uuid_chrc.uuid;
    decl->perm = BT_GATT_PERM_READ;
    decl->read = bt_gatt_attr_read_chrc;
    decl->user_data = &characteristic->chrc;

    // Attribute 2: the value itself, served by our own read/write callbacks.
    struct bt_gatt_attr *value = &self->attrs[self->attr_count++];
    value->uuid = chr_uuid;
    value->perm = perm;
    value->read = bleio_characteristic_attr_read;
    value->write = bleio_characteristic_attr_write;
    value->user_data = characteristic;

    // Attribute 3: CCCD, only when the characteristic can notify or indicate.
    if (props & (BT_GATT_CHRC_NOTIFY | BT_GATT_CHRC_INDICATE)) {
        struct _bt_gatt_ccc *ccc = &self->cccs[chr_index];
        ccc->cfg_changed = bleio_characteristic_ccc_changed;
        struct bt_gatt_attr *cccd = &self->attrs[self->attr_count++];
        cccd->uuid = &bleio_uuid_ccc.uuid;
        cccd->perm = BT_GATT_PERM_READ | BT_GATT_PERM_WRITE;
        cccd->read = bt_gatt_attr_read_ccc;
        cccd->write = bt_gatt_attr_write_ccc;
        cccd->user_data = ccc;
        characteristic->cccd_attr = cccd;
    }

    characteristic->service = self;
    characteristic->value_attr = value;
    mp_obj_list_append(self->characteristic_list, MP_OBJ_FROM_PTR(characteristic));
}
