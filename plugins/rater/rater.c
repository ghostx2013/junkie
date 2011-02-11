// -*- c-basic-offset: 4; c-backslash-column: 79; indent-tabs-mode: nil -*-
// vim:sw=4 ts=4 sts=4 expandtab
/* Copyright 2010, SecurActive.
 *
 * This file is part of Junkie.
 *
 * Junkie is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Junkie is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with Junkie.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <junkie/cpp.h>
#include <junkie/capfile.h>
#include <junkie/tools/cli.h>
#include <junkie/proto/proto.h>
#include <junkie/proto/cap.h>

struct capfile *capfile;
static char *opt_file;
static unsigned opt_max_pkts = 0;
static unsigned opt_max_size = 0;
static unsigned opt_max_secs = 0;
static unsigned opt_cap_len  = 0;
static unsigned opt_rotation = 0;
static unsigned opt_upper_bound;
static unsigned opt_lower_bound;

// Extension of the command line:
static struct cli_opt rater_opts[] = {
    { { "upper-bound", NULL }, true, "Start capturing above this throughput (bytes/sec)", CLI_SET_UINT, { .uint = &opt_upper_bound } },
    { { "lower-bound", NULL }, true, "Stop capturing below this throughput (bytes/sec)",  CLI_SET_UINT, { .uint = &opt_lower_bound } },
    { { "file",        NULL }, true, "Name of the pcap file to write",                    CLI_DUP_STR,  { .str  = &opt_file } },
    { { "max-pkts", NULL },    true, "max number of packets to capture",                  CLI_SET_UINT, { .uint = &opt_max_pkts } },
    { { "max-size", NULL },    true, "max size of the file",                              CLI_SET_UINT, { .uint = &opt_max_size } },
    { { "max-secs", NULL },    true, "max lifespan of the file (in secs)",                CLI_SET_UINT, { .uint = &opt_max_secs } },
    { { "caplen", NULL },      true, "max capture size of each packets",                  CLI_SET_UINT, { .uint = &opt_cap_len } },
    { { "rotation", NULL },    true, "when a file is done, opens another one, "
                                     "up to this number after which rotates. "
                                     "will create files suffixed with numbers.",          CLI_SET_UINT, { .uint = &opt_rotation } },
};

static void init_capture(void)
{
    if (! opt_file) return;
    capfile = capfile_new_pcap(opt_file, opt_max_pkts, opt_max_size, opt_max_secs, opt_cap_len, opt_rotation);
}

static bool inited = false;
int parse_callback(struct proto_info const *info, size_t cap_len, uint8_t const *packet)
{
    static bool writing = false;
    static struct timeval start;
    static uint64_t pld_since_start;

    ASSIGN_INFO_CHK(cap, info, 0);

    if (! inited) {
        inited = true;
        init_capture();
        writing = false;
        start = cap->tv;
        pld_since_start = 0;
    }

    // Do we started measuring payload more than 1 sec ago ?
    if (timeval_sub(&cap->tv, &start) > 1000000) {
        SLOG(LOG_DEBUG, "Current throughput is %"PRIu64" bytes/secs", pld_since_start);
        if (writing && pld_since_start < opt_lower_bound) {
            SLOG(LOG_DEBUG, "Stopping capture");
            writing = false;
        } else if (!writing && pld_since_start > opt_upper_bound) {
            SLOG(LOG_DEBUG, "Starting capture");
            writing = true;
        }
        // reset
        start = cap->tv;
        pld_since_start = 0;
    }

    pld_since_start += cap->info.payload;

    if (writing && capfile) {
        (void)capfile->ops->write(capfile, info, cap_len, packet);
    }

    return 0;
}

void on_load(void)
{
    SLOG(LOG_INFO, "Loading rater");
    cli_register("Rater plugin", rater_opts, NB_ELEMS(rater_opts));
    capfile = NULL;
    opt_file = NULL;
    inited = false;
}

void on_unload(void)
{
    SLOG(LOG_INFO, "Unloading rater");
    cli_unregister(rater_opts);
    if (capfile) {
        capfile->ops->del(capfile);
        capfile = NULL;
    }
    if (opt_file) {
        free(opt_file);
        opt_file = NULL;
    }
}