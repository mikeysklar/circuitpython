// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Mikey Sklar for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#include "py/obj.h"
#include "py/runtime.h"

#include "shared-bindings/usb_host_midi/__init__.h"
#include "shared-bindings/usb_host_midi/PortIn.h"

//| """USB host MIDI input
//|
//| The `usb_host_midi` module reads MIDI from devices plugged into a USB host
//| port. Incoming data is buffered all the time, so none is lost between reads.
//| The function names follow the mido library.
//|
//| Example::
//|
//|     import adafruit_midi
//|     import usb_host_midi
//|
//|     names = usb_host_midi.get_input_names()
//|     port = usb_host_midi.open_input(names[0])
//|     midi = adafruit_midi.MIDI(midi_in=port)
//| """
//|
//|
//| def get_input_names() -> List[str]:
//|     """Returns the names of connected MIDI devices that can send MIDI.
//|     A name is the device's product string, or ``"vid:pid"`` in hex if it
//|     has none. May wait briefly while the name is read from the device.
//|     Returns an empty list when no device is connected."""
//|     ...
//|
//|
static mp_obj_t usb_host_midi_get_input_names(void) {
    return common_hal_usb_host_midi_get_input_names();
}
MP_DEFINE_CONST_FUN_OBJ_0(usb_host_midi_get_input_names_obj, usb_host_midi_get_input_names);

//| def open_input(name: str) -> PortIn:
//|     """Opens the device with the given name for reading. Raises `ValueError`
//|     if no connected device has that name."""
//|     ...
//|
//|
static mp_obj_t usb_host_midi_open_input(mp_obj_t name) {
    mp_arg_validate_type(name, &mp_type_str, MP_QSTR_name);
    return common_hal_usb_host_midi_open_input(name);
}
MP_DEFINE_CONST_FUN_OBJ_1(usb_host_midi_open_input_obj, usb_host_midi_open_input);

static const mp_rom_map_elem_t usb_host_midi_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__),        MP_ROM_QSTR(MP_QSTR_usb_host_midi) },
    { MP_ROM_QSTR(MP_QSTR_PortIn),          MP_ROM_PTR(&usb_host_midi_portin_type) },
    { MP_ROM_QSTR(MP_QSTR_get_input_names), MP_ROM_PTR(&usb_host_midi_get_input_names_obj) },
    { MP_ROM_QSTR(MP_QSTR_open_input),      MP_ROM_PTR(&usb_host_midi_open_input_obj) },
};

static MP_DEFINE_CONST_DICT(usb_host_midi_module_globals, usb_host_midi_module_globals_table);

const mp_obj_module_t usb_host_midi_module = {
    .base = { &mp_type_module },
    .globals = (mp_obj_dict_t *)&usb_host_midi_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_usb_host_midi, usb_host_midi_module);
