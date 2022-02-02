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

#ifndef FLB_OUT_TD_HTTP_H
#define FLB_OUT_TD_HTTP_H

#include <fluent-bit/flb_config.h>
#include "td_config.h"

char *td_http_request(void *data, size_t len,
                      size_t *out_len,
                      struct flb_td *ctx, struct flb_config *config);

struct flb_http_client *td_http_client(struct flb_upstream_conn *u_conn,
                                       void *data, size_t len,
                                       char **body,
                                       struct flb_td *ctx,
                                       struct flb_config *config);
#endif
