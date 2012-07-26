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
#include <errno.h>
#include <zlib.h>
#include <sys/types.h>
#include <sys/stat.h>

#include <solv/pool.h>
#include <solv/repo.h>
#include "common_write.h"

#include "rubygems_parser.h"
#include "gem_version_bump.h"
#include "tools_util.h"

typedef struct TagsContext {
    ParseContext *pctx;
    gzFile packages;
    gzFile packages_en;
    const char *name;
    const char *basedir;
} TagsContext;

/* mkdir -p implementation */
static void mkdir_p(const char *dir) {
    char tmp[256];
    char *p = NULL;
    size_t len;
    snprintf(tmp, sizeof(tmp),"%s",dir);
    len = strlen(tmp);
    if(tmp[len - 1] == '/')
        tmp[len - 1] = 0;
    for(p = tmp + 1; *p; p++)
        if(*p == '/') {
           *p = 0;
            mkdir(tmp, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
            *p = '/';
        }
    mkdir(tmp, S_IRWXU | S_IRGRP | S_IXGRP | S_IROTH | S_IXOTH);
}

static int parse_start_callback(void *user_data)
{
    TagsContext *ctx = (TagsContext *) user_data;
    gzputs(ctx->packages, "=Ver: 2.0\n");
}

static void parse_error_callback(void *user_data, const char *msg)
{
    TagsContext *ctx = (TagsContext *) user_data;
}

static int start_callback(void *user_data, const char *file)
{
    TagsContext *ctx = (TagsContext *) user_data;
    fprintf(stderr, "file: %s\n", file);
    return 0;
}

static int end_callback(void *user_data)
{
    return 0;
}

static int deps_start_callback(void *user_data)
{
    TagsContext *ctx = (TagsContext *) user_data;
    gzputs(ctx->packages, "+Req:\n");
}

static int dep_callback(void *user_data, const char *name, const char *op, const char *version)
{
    TagsContext *ctx = (TagsContext *) user_data;
    if (*op == '~') {
      char *bumped = gem_version_bump(version);
      gzprintf(ctx->packages, "%s %s %s\n", join2(&ctx->pctx->jd, "rubygem", "-", name), ">=", version);
      gzprintf(ctx->packages, "%s %s %s\n", join2(&ctx->pctx->jd, "rubygem", "-", name), "<", bumped);
      free(bumped);
      return 0;
    }
    gzprintf(ctx->packages, "%s %s %s\n", name, op, version);
}

static int deps_end_callback(void *user_data)
{
    TagsContext *ctx = (TagsContext *) user_data;
    gzputs(ctx->packages, "-Req:\n");
}

static int attr_callback(void *user_data, const char *attr, const char *val)
{
    TagsContext *ctx = (TagsContext *) user_data;
    if (!strcmp(attr, "name"))
        ctx->name = val;
    else if (!strcmp(attr, "version")) {
        gzputs(ctx->packages, "##----------------------------------------\n");
        gzputs(ctx->packages, "=Pkg: ");
        gzprintf(ctx->packages, "%s %s %d %s\n", join2(&ctx->pctx->jd, "rubygem", "-", ctx->name), val, 0, "x86_64");
        gzputs(ctx->packages_en, "##----------------------------------------\n");
        gzputs(ctx->packages_en, "=Pkg: ");
        gzprintf(ctx->packages_en, "%s %s %d %s\n", join2(&ctx->pctx->jd, "rubygem", "-", ctx->name), val, 0, "x86_64");
    }
    else if (!strcmp(attr, "summary")) {
        gzputs(ctx->packages_en, "=Sum: ");
        gzputs(ctx->packages_en, val);
        gzputs(ctx->packages_en, "\n");
    }
    else if (!strcmp(attr, "description")) {
        gzputs(ctx->packages_en, "+Des:\n");
        gzputs(ctx->packages_en, val);
        gzputs(ctx->packages_en, "\n-Des:\n");
    }

    return 0;
}

static int parse_end_callback(void *ctx)
{
}

static void usage(const char *prog)
{
  fprintf(stderr, "Usage:\n%s <dir> ...\n", prog);
  fprintf(stderr, "<dir. is a directory with gems. The metadata will be generated there.\n");
}

int main(int argc, char **argv)
{
    int ret;
    TagsContext ctx;
    ParseContext pctx;

    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    memset(&ctx, 0, sizeof(ctx));
    gem_parse_context_initialize(&pctx);
    mkdir_p(join2(&pctx.jd, argv[1], "/", "suse/setup/descr"));

    ctx.packages = gzopen(join2(&pctx.jd, argv[1], "/", "suse/setup/descr/packages.gz"), "w");
    if (!ctx.packages) {
        fprintf(stderr, "Can't open packages.gz file: %s\n", strerror(errno));
        return 1;
    }

    ctx.packages_en = gzopen(join2(&pctx.jd, argv[1], "/", "suse/setup/descr/packages.en.gz"), "w");
    if (!ctx.packages_en) {
        fprintf(stderr, "Can't open packages.en.gz file: %s\n", strerror(errno));
        return 1;
    }


    ctx.pctx = &pctx;

    pctx.gem_parse_start_callback = parse_start_callback;
    pctx.gem_start_callback = start_callback;
    pctx.gem_parse_error_callback = parse_error_callback;
    pctx.gem_attr_callback = attr_callback;
    pctx.gem_deps_start_callback = deps_start_callback;
    pctx.gem_dep_callback = dep_callback;
    pctx.gem_deps_end_callback = deps_end_callback;
    pctx.gem_end_callback = end_callback;
    pctx.gem_parse_end_callback = parse_end_callback;
    pctx.data = &ctx;

    gem_parse(&pctx, 1, argv + 1);

    gzclose(ctx.packages);
    gzclose(ctx.packages_en);

    gem_parse_context_free(&pctx);

    return ret;
}