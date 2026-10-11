// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Mikey Sklar for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "shared-bindings/usb_host_midi/__init__.h"
#include "shared-bindings/usb_host_midi/PortIn.h"
#include "py/runtime.h"
#include "shared/runtime/interrupt_char.h"
#include "shared-module/usb/utf16le.h"
#include "supervisor/background_callback.h"
#include "supervisor/usb.h"
#include "tusb.h"

// Same wait as shared-module/usb/core/Device.c. TinyUSB's *_sync() calls run
// tuh_task() themselves, which is not safe where tuh_task() has its own task.
static volatile xfer_result_t _xfer_result;

static void _transfer_done_cb(tuh_xfer_t *xfer) {
    _xfer_result = xfer->result;
}

static bool _wait_for_callback(void) {
    while (!mp_hal_is_interrupted() &&
           _xfer_result == XFER_RESULT_INVALID) {
        RUN_BACKGROUND_TASKS;
    }
    xfer_result_t result = _xfer_result;
    _xfer_result = XFER_RESULT_INVALID;
    return result == XFER_RESULT_SUCCESS;
}

// Product string of the device, or "vid:pid" in hex if it has none.
static mp_obj_t _get_name(uint8_t idx) {
    tuh_itf_info_t info;
    if (!tuh_midi_itf_get_info(idx, &info)) {
        return mp_const_none;
    }
    uint8_t daddr = info.daddr;
    tusb_desc_device_t descriptor;
    if (tuh_descriptor_get_device_local(daddr, &descriptor) && descriptor.iProduct != 0) {
        // Two control bytes and one uint16_t language code.
        uint16_t langid_buf[2];
        _xfer_result = XFER_RESULT_INVALID;
        if (tuh_descriptor_get_string(daddr, 0, 0, langid_buf, sizeof(langid_buf), _transfer_done_cb, 0) &&
            _wait_for_callback()) {
            uint16_t temp_buf[127];
            _xfer_result = XFER_RESULT_INVALID;
            if (tuh_descriptor_get_product_string(daddr, langid_buf[1], temp_buf, sizeof(temp_buf), _transfer_done_cb, 0) &&
                _wait_for_callback()) {
                size_t utf16_len = ((temp_buf[0] & 0xff) - 2) / sizeof(uint16_t);
                if (utf16_len > 0) {
                    return utf16le_to_string(temp_buf + 1, utf16_len);
                }
            }
        }
    }
    uint16_t vid;
    uint16_t pid;
    if (!tuh_vid_pid_get(daddr, &vid, &pid)) {
        return mp_const_none;
    }
    vstr_t vstr;
    vstr_init(&vstr, 10);
    vstr_printf(&vstr, "%04x:%04x", vid, pid);
    return mp_obj_new_str_from_vstr(&vstr);
}

mp_obj_t common_hal_usb_host_midi_get_input_names(void) {
    mp_obj_t names = mp_obj_new_list(0, NULL);
    for (uint8_t idx = 0; idx < CFG_TUH_MIDI; idx++) {
        if (!tuh_midi_mounted(idx) || tuh_midi_get_rx_cable_count(idx) == 0) {
            continue;
        }
        mp_obj_t name = _get_name(idx);
        if (name != mp_const_none) {
            mp_obj_list_append(names, name);
        }
    }
    return names;
}

mp_obj_t common_hal_usb_host_midi_open_input(mp_obj_t name) {
    for (uint8_t idx = 0; idx < CFG_TUH_MIDI; idx++) {
        if (!tuh_midi_mounted(idx) || tuh_midi_get_rx_cable_count(idx) == 0) {
            continue;
        }
        if (mp_obj_equal(_get_name(idx), name)) {
            usb_host_midi_portin_obj_t *self = mp_obj_malloc(usb_host_midi_portin_obj_t, &usb_host_midi_portin_type);
            self->generation = usb_host_midi_generation();
            self->idx = idx;
            self->pending_start = 0;
            self->pending_len = 0;
            return MP_OBJ_FROM_PTR(self);
        }
    }
    mp_arg_error_invalid(MP_QSTR_name);
}
