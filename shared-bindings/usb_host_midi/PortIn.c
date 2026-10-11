// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Mikey Sklar for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include <stdint.h>

#include "shared-bindings/usb_host_midi/PortIn.h"
#include "shared-bindings/util.h"
#include "shared/runtime/context_manager_helpers.h"

#include "py/runtime.h"
#include "py/stream.h"

//| class PortIn:
//|     """Receives MIDI from a device plugged into a USB host port"""
//|
//|     def __init__(self) -> None:
//|         """You cannot create an instance of `usb_host_midi.PortIn`.
//|
//|         Use `usb_host_midi.open_input()` to get one."""
//|         ...
//|
//|     def read(self, nbytes: Optional[int] = None) -> Optional[bytes]:
//|         """Read MIDI bytes. Returns what is already buffered and does not wait.
//|         If ``nbytes`` is specified then read at most that many bytes.
//|
//|         Raises `OSError` (ENODEV) if the device was unplugged after the
//|         port was opened.
//|
//|         :return: Data read
//|         :rtype: bytes or None"""
//|         ...
//|
//|     def readinto(self, buf: WriteableBuffer, nbytes: Optional[int] = None) -> Optional[int]:
//|         """Read bytes into the ``buf``. Does not wait. If ``nbytes`` is
//|         specified then read at most that many bytes. Otherwise, read at most
//|         ``len(buf)`` bytes.
//|
//|         Raises `OSError` (ENODEV) if the device was unplugged after the
//|         port was opened.
//|
//|         :return: number of bytes read and stored into ``buf``
//|         :rtype: int or None"""
//|         ...
//|
//|     def deinit(self) -> None:
//|         """Stops reading from the device. The device keeps buffering until
//|         it is unplugged."""
//|         ...
//|
static mp_obj_t usb_host_midi_portin_deinit(mp_obj_t self_in) {
    usb_host_midi_portin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    common_hal_usb_host_midi_portin_deinit(self);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(usb_host_midi_portin_deinit_obj, usb_host_midi_portin_deinit);

static void check_for_deinit(usb_host_midi_portin_obj_t *self) {
    if (common_hal_usb_host_midi_portin_deinited(self)) {
        raise_deinited_error();
    }
}

//|     def __enter__(self) -> PortIn:
//|         """No-op used by Context Managers."""
//|         ...
//|
//  Provided by context manager helper.

//|     def __exit__(self) -> None:
//|         """Automatically deinitializes when exiting a context. See
//|         :ref:`lifetime-and-contextmanagers` for more info."""
//|         ...
//|
//  Provided by context manager helper.

// These methods are used by the shared stream methods.
static mp_uint_t usb_host_midi_portin_read(mp_obj_t self_in, void *buf_in, mp_uint_t size, int *errcode) {
    usb_host_midi_portin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    check_for_deinit(self);
    byte *buf = buf_in;

    // make sure we want at least 1 char
    if (size == 0) {
        return 0;
    }

    return common_hal_usb_host_midi_portin_read(self, buf, size, errcode);
}

static mp_uint_t usb_host_midi_portin_ioctl(mp_obj_t self_in, mp_uint_t request, uintptr_t arg, int *errcode) {
    usb_host_midi_portin_obj_t *self = MP_OBJ_TO_PTR(self_in);
    check_for_deinit(self);
    mp_uint_t ret;
    if (request == MP_STREAM_POLL) {
        mp_uint_t flags = arg;
        ret = 0;
        if ((flags & MP_STREAM_POLL_RD) && common_hal_usb_host_midi_portin_bytes_available(self) > 0) {
            ret |= MP_STREAM_POLL_RD;
        }
    } else {
        *errcode = MP_EINVAL;
        ret = MP_STREAM_ERROR;
    }
    return ret;
}

static const mp_rom_map_elem_t usb_host_midi_portin_locals_dict_table[] = {
    // Standard stream methods.
    { MP_ROM_QSTR(MP_QSTR_read),     MP_ROM_PTR(&mp_stream_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_readinto), MP_ROM_PTR(&mp_stream_readinto_obj) },

    { MP_ROM_QSTR(MP_QSTR_deinit),    MP_ROM_PTR(&usb_host_midi_portin_deinit_obj) },
    { MP_ROM_QSTR(MP_QSTR___enter__), MP_ROM_PTR(&default___enter___obj) },
    { MP_ROM_QSTR(MP_QSTR___exit__),  MP_ROM_PTR(&default___exit___obj) },
};
static MP_DEFINE_CONST_DICT(usb_host_midi_portin_locals_dict, usb_host_midi_portin_locals_dict_table);

static const mp_stream_p_t usb_host_midi_portin_stream_p = {
    .read = usb_host_midi_portin_read,
    .write = NULL,
    .ioctl = usb_host_midi_portin_ioctl,
    .is_text = false,
};

MP_DEFINE_CONST_OBJ_TYPE(
    usb_host_midi_portin_type,
    MP_QSTR_PortIn,
    MP_TYPE_FLAG_ITER_IS_ITERNEXT,
    locals_dict, &usb_host_midi_portin_locals_dict,
    iter, mp_stream_unbuffered_iter,
    protocol, &usb_host_midi_portin_stream_p
    );
