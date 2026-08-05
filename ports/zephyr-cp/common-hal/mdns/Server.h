// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Adafruit Industries LLC
//
// SPDX-License-Identifier: MIT

#pragma once

#include "py/obj.h"

#include <zephyr/net/dns_sd.h>

// RFC 6763 Section 7.2.
#define MDNS_MAX_SERVICES 4
// Encoded as length-prefixed key=value pairs back to back; 256 bytes is
// generous for the handful of records a board actually advertises.
#define MDNS_MAX_TXT_BUF 256

typedef struct {
    mp_obj_base_t base;
    const char *instance_name;

    // Parallel to each other by index. records[] is what gets handed to
    // Zephyr's mDNS responder (mdns_responder_set_ext_records() keeps the
    // pointer, not a copy, so these must stay put for the object's life);
    // service_types[]/ports[]/text[] are the backing storage each record
    // points into.
    struct dns_sd_rec records[MDNS_MAX_SERVICES];
    const char *service_types[MDNS_MAX_SERVICES];
    uint16_t ports[MDNS_MAX_SERVICES];
    char text[MDNS_MAX_SERVICES][MDNS_MAX_TXT_BUF];
    size_t num_services;

    // Track if this object owns the underlying mDNS responder.
    bool inited;
} mdns_server_obj_t;
