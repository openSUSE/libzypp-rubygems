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

#include <solv/pool.h>
#include <solv/repo.h>
#include "common_write.h"

#include "rubygems_parser.h"
#include "gem_version_bump.h"
#include "tools_util.h"

typedef struct SolvContext {
    Repo *repo;
    Solvable *s;
    Repodata *data;
    int flags;
    ParseContext *pctx;
} SolvContext;

static int parse_start_callback(void *user_data)
{
}

static void parse_error_callback(void *user_data, const char *msg)
{
    SolvContext *ctx = (SolvContext *) user_data;
    pool_error(ctx->repo->pool, -1, msg);
}

static int start_callback(void *user_data, const char *file)
{
    SolvContext *ctx = (SolvContext *) user_data;
    ctx->s = pool_id2solvable(ctx->repo->pool, repo_add_solvable(ctx->repo));
    repodata_set_poolstr(ctx->data, ctx->s - ctx->repo->pool->solvables, SOLVABLE_GROUP, "Devel/Languages/Ruby");
    ctx->s->arch = pool_str2id(ctx->repo->pool, "x86_64", 1);
    return 0;
}

static int end_callback(void *user_data)
{
    return 0;
}

static int dep_callback(void *user_data, const char *name, const char *op, const char *version)
{
    SolvContext *ctx = (SolvContext *) user_data;
    int flags = 0;

    if (*op == '~') {
        ctx->s->requires = repo_addid_dep(ctx->s->repo, ctx->s->requires,
        pool_rel2id(ctx->s->repo->pool,
            pool_str2id(ctx->s->repo->pool, join2(&ctx->pctx->jd, "rubygem", "-", name), 1),
            pool_str2id(ctx->s->repo->pool, version, 1),
            REL_GT | REL_EQ, 1),
        0);

        char *bumped = gem_version_bump(version);
        ctx->s->requires = repo_addid_dep(ctx->s->repo, ctx->s->requires,
        pool_rel2id(ctx->s->repo->pool,
            pool_str2id(ctx->s->repo->pool, join2(&ctx->pctx->jd, "rubygem", "-", name), 1),
            pool_str2id(ctx->s->repo->pool, bumped, 1),
            REL_LT, 1),
        0);
        free(bumped);
        return 0;
    }

    const char *fbp;
    for (fbp = op;; fbp++)
    {
        if (*fbp == '>')
            flags |= REL_GT;
        else if (*fbp == '=')
            flags |= REL_EQ;
        else if (*fbp == '<')
            flags |= REL_LT;
        else
            break;
    }

    ctx->s->requires = repo_addid_dep(ctx->s->repo, ctx->s->requires,
        pool_rel2id(ctx->s->repo->pool,
            pool_str2id(ctx->s->repo->pool, join2(&ctx->pctx->jd, "rubygem", "-", name), 1),
            pool_str2id(ctx->s->repo->pool, version, 1),
            flags, 1),
        0);
}

static int attr_callback(void *user_data, const char*attr, const char *val)
{
    SolvContext *ctx = (SolvContext *) user_data;
    Id handle = ctx->s - ctx->s->repo->pool->solvables;
    if (!strcmp(attr, "name"))
        ctx->s->name = pool_str2id(ctx->s->repo->pool, join2(&ctx->pctx->jd, "rubygem", "-", val), 1);
      //ctx->s->name = pool_str2id(ctx->s->repo->pool, val, 1);
    else if (!strcmp(attr, "version"))
        ctx->s->evr = pool_str2id(ctx->s->repo->pool, val, 1);
    else if (!strcmp(attr, "homepage"))
        repodata_set_str(ctx->data, handle, SOLVABLE_URL, val);
    else if (!strcmp(attr, "summary"))
       repodata_set_str(ctx->data, handle, SOLVABLE_SUMMARY, val);
    else if (!strcmp(attr, "description"))
      repodata_set_str(ctx->data, handle, SOLVABLE_DESCRIPTION, val);
    return 0;
}

static int parse_end_callback(void *user_data)
{
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
    int flags = 0;
    Pool *pool = pool_create();
    Repo *repo = repo_create(pool, "rubygems");
    Repodata *data = repo_add_repodata(repo, flags);
    char *basefile = 0;

    SolvContext ctx;
    ParseContext pctx;

    memset(&ctx, 0, sizeof(ctx));
    gem_parse_context_initialize(&pctx);

    ctx.repo = repo;
    ctx.data = data;
    ctx.pctx = &pctx;

    pctx.gem_parse_start_callback = parse_start_callback;
    pctx.gem_start_callback = start_callback;
    pctx.gem_parse_error_callback = parse_error_callback;
    pctx.gem_attr_callback = attr_callback;
    pctx.gem_dep_callback = dep_callback;
    pctx.gem_end_callback = end_callback;
    pctx.gem_parse_end_callback = parse_end_callback;
    pctx.data = &ctx;

    gem_parse(&pctx, argc -1, argv + 1);

    gem_parse_context_free(&pctx);

    if (!(flags & REPO_NO_INTERNALIZE))
      repodata_internalize(data);

    tool_write(repo, basefile, 0);
    pool_free(pool);

    return ret;
}