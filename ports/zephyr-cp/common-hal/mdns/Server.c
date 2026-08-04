// This file is part of the CircuitPython project: https://circuitpython.org
//
// SPDX-FileCopyrightText: Copyright (c) 2026 Adafruit Industries LLC
//
// SPDX-License-Identifier: MIT

#include <string.h>
#include <stdio.h>

#include "shared-bindings/mdns/Server.h"

#include "py/runtime.h"
#include "shared-bindings/wifi/__init__.h"
#include "shared-bindings/wifi/Radio.h"
#include "shared-bindings/mdns/RemoteService.h"

#include <zephyr/net/hostname.h>
#include <zephyr/net/dns_sd.h>
#include <zephyr/net/mdns_responder.h>

// Track if we are globally inited. Mirrors every other port: one live
// mdns.Server at a time. A second construct call marks its object deinited
// rather than erroring, so web_workflow.c's own instance and a user's
// `mdns.Server(wifi.radio)` don't fight over the same responder.
static bool object_inited = false;

void mdns_server_construct(mdns_server_obj_t *self, bool workflow) {
    if (object_inited) {
        self->inited = false;
        return;
    }
    self->inited = true;
    object_inited = true;
    self->num_services = 0;
    self->instance_name = "";

    uint8_t mac[6];
    wifi_radio_get_mac_address(&common_hal_wifi_radio_obj, mac);
    char default_hostname[sizeof("cpy-XXXXXX")];
    snprintf(default_hostname, sizeof(default_hostname), "cpy-%02x%02x%02x", mac[3], mac[4], mac[5]);
    net_hostname_set(default_hostname, strlen(default_hostname));

    // Other ports also answer "circuitpython.local" as a secondary alias
    // when `workflow` is true. Zephyr's mDNS responder only ever answers
    // for the single net_hostname() value -- there's no secondary-hostname
    // registration to hook up here.
    (void)workflow;
}

void common_hal_mdns_server_construct(mdns_server_obj_t *self, mp_obj_t network_interface) {
    if (network_interface != MP_OBJ_FROM_PTR(&common_hal_wifi_radio_obj)) {
        mp_raise_ValueError(MP_ERROR_TEXT("mDNS only works with built-in WiFi"));
        return;
    }
    if (object_inited) {
        mp_raise_RuntimeError(MP_ERROR_TEXT("mDNS already initialized"));
    }
    mdns_server_construct(self, false);
}

void common_hal_mdns_server_deinit(mdns_server_obj_t *self) {
    if (common_hal_mdns_server_deinited(self)) {
        return;
    }
    self->inited = false;
    object_inited = false;
    // Zephyr's mdns_responder_set_ext_records() has no way to clear the
    // active record set -- passing NULL/0 is rejected outright (-EINVAL) --
    // so whatever was last advertised keeps answering queries after deinit.
    // Nothing in this port currently deinits and re-constructs mDNS at
    // runtime, so this is a latent gap rather than an observed one.
}

bool common_hal_mdns_server_deinited(mdns_server_obj_t *self) {
    return !self->inited;
}

const char *common_hal_mdns_server_get_hostname(mdns_server_obj_t *self) {
    return net_hostname_get();
}

void common_hal_mdns_server_set_hostname(mdns_server_obj_t *self, const char *hostname) {
    if (net_hostname_set(hostname, strlen(hostname)) != 0) {
        mp_raise_RuntimeError(MP_ERROR_TEXT("Failed to set hostname"));
    }
}

const char *common_hal_mdns_server_get_instance_name(mdns_server_obj_t *self) {
    return self->instance_name;
}

void common_hal_mdns_server_set_instance_name(mdns_server_obj_t *self, const char *instance_name) {
    self->instance_name = instance_name;
}

// Zephyr's net/lib/dns ships an mDNS *responder* (subsys/net/lib/dns/mdns_responder.c)
// but no client capable of sending PTR/SRV queries and parsing replies --
// dns_resolve_name(..., DNS_QUERY_TYPE_PTR, ...) resolves one already-known
// name, it doesn't browse a service type. There's nothing to discover here
// yet, so report no results rather than pretending to search.
size_t mdns_server_find(mdns_server_obj_t *self, const char *service_type, const char *protocol,
    mp_float_t timeout, mdns_remoteservice_obj_t *out, size_t out_len) {
    return 0;
}

mp_obj_t common_hal_mdns_server_find(mdns_server_obj_t *self, const char *service_type, const char *protocol, mp_float_t timeout) {
    return mp_const_empty_tuple;
}

static size_t encode_txt_records(char *buf, size_t buf_len, const char *txt_records[], size_t num_txt_records) {
    size_t off = 0;
    for (size_t i = 0; i < num_txt_records; i++) {
        size_t len = strlen(txt_records[i]);
        if (len > 255 || off + 1 + len > buf_len) {
            mp_raise_RuntimeError(MP_ERROR_TEXT("Failed to add service TXT record"));
        }
        buf[off++] = (char)len;
        memcpy(buf + off, txt_records[i], len);
        off += len;
    }
    return off;
}

void common_hal_mdns_server_advertise_service(mdns_server_obj_t *self, const char *service_type, const char *protocol, mp_int_t port, const char *txt_records[], size_t num_txt_records) {
    // Replace the existing record if this service_type was already advertised.
    size_t slot = self->num_services;
    for (size_t i = 0; i < self->num_services; i++) {
        if (service_type == self->service_types[i] || strcmp(service_type, self->service_types[i]) == 0) {
            slot = i;
            break;
        }
    }
    if (slot == self->num_services) {
        if (self->num_services >= MDNS_MAX_SERVICES) {
            mp_raise_RuntimeError(MP_ERROR_TEXT("Out of MDNS service slots"));
            return;
        }
        self->num_services++;
    }

    self->service_types[slot] = service_type;
    self->ports[slot] = sys_cpu_to_be16((uint16_t)port);

    size_t text_size = encode_txt_records(self->text[slot], MDNS_MAX_TXT_BUF, txt_records, num_txt_records);

    struct dns_sd_rec *rec = &self->records[slot];
    rec->instance = self->instance_name;
    rec->service = service_type;
    rec->proto = protocol;
    rec->domain = "local";
    rec->port = &self->ports[slot];
    if (text_size == 0) {
        rec->text = dns_sd_empty_txt;
        rec->text_size = sizeof(dns_sd_empty_txt);
    } else {
        rec->text = self->text[slot];
        rec->text_size = text_size;
    }

    mdns_responder_set_ext_records(self->records, self->num_services);
}
