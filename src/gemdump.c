/*
 * Copyright (c) 2012, Novell Inc.
 *
 * This program is licensed under the BSD license, read LICENSE.BSD
 * for further information
 *
 * rubygem2solv: Parses gem files and generates solv data.
 *
 * Author: Duncan Mac-Vicar P. <dmacvicar@suse.de>
 */

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include "rubygems_parser.h"

typedef struct DumpContext {
} DumpContext;

static int parse_start_callback(void *user_data)
{
    printf("start!\n");
}

static void parse_error_callback(void *user_data, const char *msg)
{
    fprintf(stderr, msg);
}

static int attr_callback(void *user_data, const char*attr, const char *val)
{
    printf("  %s = %s\n", attr, val);
    return 0;
}

static int parse_end_callback(void *user_data)
{
    printf("end!\n");
}

static void usage(const char *prog)
{
  fprintf(stderr, "Usage:\n%s [options] arg1 arg2 arg3 ...\n", prog);
  fprintf(stderr, "You can pass one or more gem files or directories with gems.\n");
  fprintf(stderr, "You can pass one or more gem files or directories with gems.\n");
  fprintf(stderr, "options: -b $file : output to $file instead of stdout.\n");
}

int main(int argc, char **argv)
{
    int ret;
    DumpContext ctx;
    ParseContext pctx;

    memset(&ctx, 0, sizeof(ctx));
    gem_parse_context_initialize(&pctx);

    pctx.gem_parse_start_callback = parse_start_callback;
    pctx.gem_parse_error_callback = parse_error_callback;
    pctx.gem_attr_callback = attr_callback;
    pctx.gem_parse_end_callback = parse_end_callback;
    pctx.data = &ctx;

    gem_parse(&pctx, argc, argv);

    gem_parse_context_free(&pctx);

    return ret;
}