// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Mikey Sklar for Adafruit Industries
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"

// Returns a new list of names of connected MIDI devices that have an IN
// endpoint. Empty when none are connected. Allocates, may raise.
mp_obj_t common_hal_usb_host_midi_get_input_names(void);

// Returns a new PortIn for the device named name. Raises ValueError if no
// connected device has that name.
mp_obj_t common_hal_usb_host_midi_open_input(mp_obj_t name);
