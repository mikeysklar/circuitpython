// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Adafruit Industries LLC
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"

// Populated by Server.find(), which Zephyr's mDNS stack can't implement yet
// (see common-hal/mdns/Server.c) -- kept the same shape as the other ports'
// so this only needs to grow, not change shape, once that lands upstream.
typedef struct {
    mp_obj_base_t base;
    uint32_t ipv4_address;
    uint16_t port;
    char protocol[5]; // RFC 6763 Section 7.2 - 4 bytes + 1 for NUL
    char service_name[17]; // RFC 6763 Section 7.2 - 16 bytes + 1 for NUL
    char instance_name[64]; // RFC 6763 Section 7.2 - 63 bytes + 1 for NUL
    char hostname[64]; // RFC 6762 Appendix A - 63 bytes for label + 1 for NUL
} mdns_remoteservice_obj_t;
