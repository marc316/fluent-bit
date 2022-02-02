/* -*- Mode: C; tab-width: 4; indent-tabs-mode: nil; c-basic-offset: 4 -*- */

/*  Fluent Bit
 *  ==========
 *  Copyright (C) 2015-2022 The Fluent Bit Authors
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */

/*
 * This module implements the binary network protocol of collectd.
 * (https://collectd.org/wiki/index.php/Binary_protocol)
 *
 * The only interface you need to care is netprot_to_msgpack() that
 * parses a UDP packet and converts it into MessagePack format.
 */

#include <fluent-bit/flb_compat.h>
#include <fluent-bit/flb_log.h>
#include <fluent-bit/flb_pack.h>
#include <fluent-bit/flb_endian.h>
#include <msgpack.h>
#include "netprot.h"
#include "typesdb.h"

#define be16read(x) (be16toh(*(uint16_t *) (x)))
#define be32read(x) (be32toh(*(uint32_t *) (x)))
#define be64read(x) (be64toh(*(uint64_t *) (x)))

#define le16read(x) (le16toh(*(uint16_t *) (x)))
#define le32read(x) (le32toh(*(uint32_t *) (x)))
#define le64read(x) (le64toh(*(uint64_t *) (x)))

/* Convert a high-resolution time into a normal UNIX time. */
#define hr2time(x) ((double) (x) / 1073741824)

/* Basic data field definitions for collectd */
#define PART_HOST            0x0000
#define PART_TIME            0x0001
#define PART_PLUGIN          0x0002
#define PART_PLUGIN_INSTANCE 0x0003
#define PART_TYPE            0x0004
#define PART_TYPE_INSTANCE   0x0005
#define PART_VALUE           0x0006
#define PART_INTERVAL        0x0007

#define PART_TIME_HR         0x0008
#define PART_INTERVAL_HR     0x0009

/*
 * The "DS_TYPE_*" are value types for PART_VALUE fields.
 *
 * Read https://collectd.org/wiki/index.php/Data_source for what
 * these types mean.
 */
#define DS_TYPE_COUNTER   0
#define DS_TYPE_GAUGE     1
#define DS_TYPE_DERIVE    2
#define DS_TYPE_ABSOLUTE  3

struct netprot_header
{
    double time;
    double interval;
    char *host;
    char *plugin;
    char *plugin_instance;
    char *type;
    char *type_instance;
};

static int netprot_pack_cstr(msgpack_packer *ppck, char *s) {
    int len = strlen(s);
    msgpack_pack_str(ppck, len);
    msgpack_pack_str_body(ppck, s, len);
    return 0;
}

/* Return the number of non-empty fields in the header */
static int netprot_header_count(struct netprot_header *hdr)
{
    return ((hdr->time > 0)
            + (hdr->interval > 0)
            + !!hdr->host
            + !!hdr->type
            + !!hdr->type_instance
            + !!hdr->plugin
            + !!hdr->plugin_instance);
}

static int netprot_pack_value(char *ptr, int size, struct netprot_header *hdr,
                              struct mk_list *tdb, msgpack_packer *ppck)
{
    int i;
    char type;
    char *pval;
    uint16_t count;
    struct typesdb_node *node;

    if (hdr->type == NULL) {
        flb_error("[in_collectd] invalid data (type is NULL)");
        return -1;
    }

    /*
     * Since each value uses (1 + 8) bytes, the total buffer size must
     * be 2-byte header plus <count * 9> bytes.
     */
    count = be16read(ptr);
    if (size != 2 + count * 9) {
        flb_error("[in_collectd] data corrupted (size=%i, count=%i)",
                  size, count);
        return -1;
    }

    /*
     * We need to query against TypesDB in order to get field names
     * for the data set values.
     */
    node = typesdb_find_node(tdb, hdr->type);
    if (!node) {
        flb_error("[in_collectd] no such type found '%s'", hdr->type);
        return -1;
    }
    if (node->count != count) {
        flb_error("[in_collectd] invalid value for '%s' (%i != %i)",
                  hdr->type, node->count, count);
        return -1;
    }

    msgpack_pack_array(ppck, 2);
    flb_pack_time_now(ppck);

    msgpack_pack_map(ppck, netprot_header_count(hdr) + count);

    netprot_pack_cstr(ppck, "type");
    netprot_pack_cstr(ppck, hdr->type);

    if (hdr->type_instance) {
        netprot_pack_cstr(ppck, "type_instance");
        netprot_pack_cstr(ppck, hdr->type_instance);
    }

    if (hdr->time > 0) {
        netprot_pack_cstr(ppck, "time");
        msgpack_pack_double(ppck, hdr->time);
    }

    if (hdr->interval > 0) {
        netprot_pack_cstr(ppck, "interval");
        msgpack_pack_double(ppck, hdr->interval);
    }

    if (hdr->plugin) {
        netprot_pack_cstr(ppck, "plugin");
        netprot_pack_cstr(ppck, hdr->plugin);
    }

    if (hdr->plugin_instance) {
        netprot_pack_cstr(ppck, "plugin_instance");
        netprot_pack_cstr(ppck, hdr->plugin_instance);
    }

    if (hdr->host) {
        netprot_pack_cstr(ppck, "host");
        netprot_pack_cstr(ppck, hdr->host);
    }

    for (i = 0; i < count; i++) {
        pval = ptr + 2 + count + 8 * i;
        type = ptr[2 + i];

        netprot_pack_cstr(ppck, node->fields[i]);

        switch (type) {
            case DS_TYPE_COUNTER:
                msgpack_pack_uint64(ppck, be64read(pval));
                break;
            case DS_TYPE_GAUGE:
                msgpack_pack_double(ppck, *((double *) pval));
                break;
            case DS_TYPE_DERIVE:
                msgpack_pack_int64(ppck, (int64_t) be64read(pval));
                break;
            case DS_TYPE_ABSOLUTE:
                msgpack_pack_uint64(ppck, be64read(pval));
                break;
            default:
                flb_error("[in_collectd] unknown data type %i", type);
                return -1;
        }
    }
    return 0;
}

/*
 * Entry point function
 */
int netprot_to_msgpack(char *buf, int len, struct mk_list *tdb,
                       msgpack_packer *ppck)
{
    uint16_t part_type;
    uint16_t part_len;
    int size;
    char *ptr;
    struct netprot_header hdr = {0};

    while (len >= 4) {
        part_type = be16read(buf);
        part_len = be16read(buf + 2);

        if (len < part_len) {
            flb_error("[in_collectd] data truncated (%i < %i)", len, part_len);
            return -1;
        }
        ptr = buf + 4;
        size = part_len - 4;

        switch (part_type) {
            case PART_HOST:
                if (ptr[size] == '\0') {
                    hdr.host = ptr;
                }
                break;
            case PART_TIME:
                hdr.time = (double) be64read(ptr);
                break;
            case PART_TIME_HR:
                hdr.time = hr2time(be64read(ptr));
                break;
            case PART_PLUGIN:
                if (ptr[size] == '\0') {
                    hdr.plugin = ptr;
                }
                break;
            case PART_PLUGIN_INSTANCE:
                if (ptr[size] == '\0') {
                    hdr.plugin_instance = ptr;
                }
                break;
            case PART_TYPE:
                if (ptr[size] == '\0') {
                    hdr.type = ptr;
                }
                break;
            case PART_TYPE_INSTANCE:
                if (ptr[size] == '\0') {
                    hdr.type_instance = ptr;
                }
                break;
            case PART_VALUE:
                if (netprot_pack_value(ptr, size, &hdr, tdb, ppck)) {
                    return -1;
                }
                break;
            case PART_INTERVAL:
                hdr.interval = (double) be64read(ptr);
                break;
            case PART_INTERVAL_HR:
                hdr.interval = hr2time(be64read(ptr));
                break;
            default:
                flb_debug("[in_collectd] skip unknown type %x", part_type);
                break;
        }
        len -= part_len;
        buf += part_len;
    }
    return 0;
}
