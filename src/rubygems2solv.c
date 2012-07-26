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
#include <stdlib.h>
#include <zlib.h>
#include <glob.h>
#include <errno.h>
#include <sys/stat.h>

#include <yaml.h>

#include <archive.h>
#include <archive_entry.h>

#include <solv/pool.h>
#include <solv/repo.h>
#include "common_write.h"
#include "tools_util.h"

#define BLOCK_SIZE 16384
#define ZLIB_BUFFER_SIZE 64000
#define RUBYGEM_GZIP_HEADER_LEN 10

typedef struct GemContext {
    yaml_document_t *doc;
    Solvable *s;
    Repodata *data;
    struct joindata jd;
} GemContext;

static unsigned char *
decompress(unsigned char *in, int inl, int *outlp)
{
  z_stream strm;
  int outl, ret;
  unsigned char *out;
  memset(&strm, 0, sizeof(strm));
  strm.next_in = in;
  strm.avail_in = inl;
  out = malloc(4096);
  strm.next_out = out;
  strm.avail_out = 4096;
  outl = 0;
  ret = inflateInit2(&strm, -MAX_WBITS);
  if (ret != Z_OK)
    {
      inflateEnd(&strm);
      free(out);
      return 0;
    }
  for (;;)
    {
      if (strm.avail_out == 0)
    {
      outl += 4096;
      out = realloc(out, outl + 4096);
      strm.next_out = out + outl;
      strm.avail_out = 4096;
    }
      ret = inflate(&strm, Z_NO_FLUSH);
      if (ret == Z_STREAM_END)
    break;
      if (ret != Z_OK)
    {
      fprintf(stderr, "Error decompressing: %s\n", strm.msg);
      inflateEnd(&strm);
      free(out);
      return 0;
    }
    }
  outl += 4096 - strm.avail_out;
  inflateEnd(&strm);
  *outlp = outl;
  return out;
}

/**
 * Reads the content of the metadata from the archive
 * returning an allocated buffer pointing to compressed gz data
 * and the size on outlp.
 */
static unsigned char *
read_archive(struct archive *archive, int *outlp)
{
  int outl = 0;
  unsigned char *out;
  out = malloc(4096);
  int ret;

  while ((ret = archive_read_data(archive, out, 4096))) {
    if (ret == -1) {
        free(out);
        return 0;
    }
    outl += ret;
    out = realloc(out, outl + 4096);
  }
  *outlp = outl;
  return out;
}

static yaml_node_t* yaml_get_seq_node(yaml_document_t *doc, yaml_node_t *node, int index)
{

    yaml_node_item_t *i;
    for (i = node->data.sequence.items.start; i < node->data.sequence.items.top; ++i)
    {
        if ((i - node->data.sequence.items.start) == index) {
          return yaml_document_get_node(doc, *i);
        }
    }
    return 0;
}

static const char * yaml_get_seq_node_str(yaml_document_t *doc, yaml_node_t *node, int index)
{
    yaml_node_t *valNode = yaml_get_seq_node(doc, node, index);
    if (valNode && valNode->type == YAML_SCALAR_NODE) {
        return (const char *) valNode->data.scalar.value;
    }
    return 0;
}

/* [] operator for a yaml mapping node */
static yaml_node_t* yaml_get_map_node(yaml_document_t *doc, yaml_node_t *node, const char *key_str)
{
    yaml_node_pair_t *i;
    for (i = node->data.mapping.pairs.start; i < node->data.mapping.pairs.top; ++i)
    {
        yaml_node_t *key = yaml_document_get_node(doc, i->key);
        yaml_node_t *value = yaml_document_get_node(doc, i->value);

        if (!strcmp((const char *) key->data.scalar.value, key_str))
          return value;
    }
    return 0;
}

/* [] operator for a yaml mapping node with a string value*/
static const char * yaml_get_map_node_str(yaml_document_t *doc, yaml_node_t *node, const char *key)
{
    yaml_node_t *valNode = yaml_get_map_node(doc, node, key);
    if (valNode && valNode->type == YAML_SCALAR_NODE) {
      return (const char *) valNode->data.scalar.value;
    }
    return 0;
}

static int parse_attribute(GemContext *ctx, const char *attr, const char *val)
{
    Id handle = ctx->s - ctx->s->repo->pool->solvables;
    if (!strcmp(attr, "name"))
        ctx->s->name = pool_str2id(ctx->s->repo->pool, join2(&ctx->jd, "rubygem", "-", val), 1);
      //ctx->s->name = pool_str2id(ctx->s->repo->pool, val, 1);
    if (!strcmp(attr, "homepage"))
        repodata_set_str(ctx->data, handle, SOLVABLE_URL, val);
    if (!strcmp(attr, "summary"))
       repodata_set_str(ctx->data, handle, SOLVABLE_SUMMARY, val);
    if (!strcmp(attr, "description"))
      repodata_set_str(ctx->data, handle, SOLVABLE_DESCRIPTION, val);
    return 0;

}

static int parse_version(GemContext *ctx, yaml_node_t *node)
{
    /*
      version: 0.4.1
    */
    const char *version = yaml_get_map_node_str(ctx->doc, node, "version");

    if (!version) {
      return pool_error(ctx->s->repo->pool, -1, "Error parsing version");
    }

    ctx->s->evr = pool_str2id(ctx->s->repo->pool, version, 1);
  return 0;
}

static int parse_requirement(GemContext *ctx, const char *name, yaml_node_t *node_req)
{
    /*
    - ">="
    - !ruby/object:Gem::Version
      version: 1.0.5
    */
    if (node_req->type != YAML_SEQUENCE_NODE) {
          return pool_error(ctx->s->repo->pool, -1, "Error parsing requirement");
    }
    const char *op = yaml_get_seq_node_str(ctx->doc, node_req, 0);
    if (!op)
      return -1;

    yaml_node_t *version_node = yaml_get_seq_node(ctx->doc, node_req, 1);
    if (!version_node)
      return -1;

    const char *version = yaml_get_map_node_str(ctx->doc, version_node, "version");
    if (!version)
      return -1;

    int flags = 0;

    char *fbp;
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
                pool_str2id(ctx->s->repo->pool, join2(&ctx->jd, "rubygem", "-", name), 1),
                pool_str2id(ctx->s->repo->pool, version, 1),
            flags, 1),
        0);
    return 0;
}

static int parse_dependency(GemContext *ctx, yaml_node_t *node)
{
    /*
      name: trollop
      type: :runtime
      version_requirement:
      version_requirements: !ruby/object:Gem::Requirement
        requirements:
        - - ">="
          - !ruby/object:Gem::Version
            version: 1.0.5
        version:
    */
    if (node->type != YAML_MAPPING_NODE) {
        return pool_error(ctx->s->repo->pool, -1, "Error parsing dependency");
    }

    const char *name = yaml_get_map_node_str(ctx->doc, node, "name");
    if (!name)
      return -1;

    yaml_node_t *version_reqs = yaml_get_map_node(ctx->doc, node, "version_requirements");
    if (!version_reqs)
        return -1;

    yaml_node_t *reqs = yaml_get_map_node(ctx->doc, version_reqs, "requirements");
    if (!reqs)
        return -1;

    /* iterate over requirements, usually only one */
    yaml_node_item_t *i;
    for (i = reqs->data.sequence.items.start; i < reqs->data.sequence.items.top; ++i)
    {
        yaml_node_t *item = yaml_document_get_node(ctx->doc, *i);
        parse_requirement(ctx, name, item);
    }
    return 0;
}

static int parse_dependencies(GemContext *ctx,  yaml_node_t *node)
{
    /*
      - !ruby/object:Gem::Dependency
        name: trollop
        type: :runtime
        ...
      - !ruby/object:Gem::Dependency
        name: log4r
        type: :runtime
        ...
    */
    if (node->type != YAML_SEQUENCE_NODE) {
        return pool_error(ctx->s->repo->pool, -1, "Error parsing deps");
    }

    yaml_node_item_t *i;
    for (i = node->data.sequence.items.start; i < node->data.sequence.items.top; ++i)
    {
        yaml_node_t *item = yaml_document_get_node(ctx->doc, *i);

        parse_dependency(ctx, item);
    }
    return 0;
}

int parse_root_node(GemContext *ctx, yaml_node_t *node)
{
    yaml_node_pair_t *i;
    for (i = node->data.mapping.pairs.start; i < node->data.mapping.pairs.top; ++i)
    {
        yaml_node_t *key = yaml_document_get_node(ctx->doc, i->key);
        yaml_node_t *value = yaml_document_get_node(ctx->doc, i->value);

        if (value->type == YAML_SCALAR_NODE) {
            /*fprintf(stderr, "%s -> %s\n", key->data.scalar.value, value->data.scalar.value);*/
            parse_attribute(ctx, (const char *) key->data.scalar.value, (const char *) value->data.scalar.value);
        }
        else
        {
            if (!strcmp((const char *) key->data.scalar.value, "dependencies"))
                parse_dependencies(ctx, value);
            else if (!strcmp((const char *) key->data.scalar.value, "version"))
                parse_version(ctx, value);
        }
    }
    return 0;
}

static int parse_metadata_entry(Repo *repo, Repodata *data, struct archive *a, struct archive_entry *entry)
{
    yaml_parser_t parser;
    yaml_document_t document;
    int ret = 0;

    unsigned char *metadata_gz;
    unsigned char *metadata;

    Solvable *s;
    Pool *pool = repo->pool;

    int metadata_gz_len = 0;
    int metadata_len = 0;

    metadata_gz = read_archive(a, &metadata_gz_len);
    if (!metadata_gz) {

        pool_error(pool, -1, "Error reading gem archive: %s", archive_error_string(a));
        free(metadata_gz);
        return -1;
    }

    metadata = decompress(metadata_gz + RUBYGEM_GZIP_HEADER_LEN, metadata_gz_len - RUBYGEM_GZIP_HEADER_LEN, &metadata_len);
    free(metadata_gz);

    if (!metadata) {
        pool_error(pool, -1, "Error decompressing (%d) -> (%d)", metadata_gz_len, metadata_len);
        free(metadata);
        return -1;
    }

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, metadata, metadata_len);

    ret = !yaml_parser_load(&parser, &document);
    if (ret != 0) {
        pool_error(pool, -1, "Error parsing YAML document");
        free(metadata);
        yaml_parser_delete(&parser);
        yaml_document_delete(&document);
        return -1;
    }

    yaml_node_t *root = yaml_document_get_root_node(&document);
    yaml_parser_delete(&parser);
    if (!root) {
        pool_error(pool, -1, "Error getting YAML document root node");
        yaml_document_delete(&document);
        return -1;
    }

    s = pool_id2solvable(pool, repo_add_solvable(repo));
    repodata_set_poolstr(data, s - pool->solvables, SOLVABLE_GROUP, "Devel/Languages/Ruby");
    s->arch = pool_str2id(pool, "x86_64", 1);

    GemContext ctx;
    ctx.s = s;
    ctx.doc = &document;
    ctx.data = data;
    memset(&ctx.jd, 0, sizeof(ctx.jd));

    ret = parse_root_node(&ctx, root);
    yaml_document_delete(&document);

    free(metadata);
    join_freemem(&ctx.jd);

    if (ret != 0) {
        return pool_error(pool, -1, "Error parsing YAML document");
    }
    return 0;
}

int repo_add_rubygem(Repo *repo, const char *rubygem, int flags)
{
    Pool *pool = repo->pool;

    struct archive *a;
    struct archive_entry *entry;
    int ret;

    Repodata *data = repo_add_repodata(repo, flags);

    a = archive_read_new();
    archive_read_support_compression_gzip(a);
    archive_read_support_format_tar(a);

    ret = archive_read_open_filename(a, rubygem, BLOCK_SIZE);
    if (ret != ARCHIVE_OK) goto out;

    ret = -1;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (!strcmp(archive_entry_pathname(entry), "metadata.gz"))
            ret = parse_metadata_entry(repo, data, a, entry);
        else
          archive_read_data_skip(a);
    }
out:
    if (ret != 0) {
      pool_error(pool, -1, "Error reading gem file %s: %s", rubygem, archive_error_string(a));
    }
    archive_read_finish(a);

    if (!(flags & REPO_NO_INTERNALIZE))
      repodata_internalize(data);

    return ret;
}

int repo_add_rubygem_dir(Repo *repo, const char *dir, int flags)
{
    glob_t data;
    struct joindata jd;
    memset(&jd, 0, sizeof(jd));
    int ret = 0;

    switch( glob(join2(&jd, dir, "/", "*.gem"), 0, NULL, &data))
    {
        case 0:
            break;
        case GLOB_NOSPACE:
            fprintf(stderr, "Out of memory\n");
            return -1;
        case GLOB_ABORTED:
            fprintf(stderr, "Reading error\n");
            return -1;
        case GLOB_NOMATCH:
            fprintf(stderr, "No files found\n");
            return -1;
        default:
            break;
    }

    int i;
    for(i=0; i<data.gl_pathc; i++)
    {
        if (repo_add_rubygem(repo, data.gl_pathv[i], flags) != 0)
          ret = -1;
    }

    globfree( &data );
    join_freemem(&jd);
    return ret;
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
    Pool *pool = pool_create();
    int flags = 0;
    int status;
    struct stat st_buf;
    const char *basefile = 0;
    int c;

    while ((c = getopt(argc, argv, "b::")) >= 0) {
        switch(c) {
        case 'b':
            basefile = optarg;
            break;
        default:
          usage(argv[0]);
          return 1;
        }
    }

    Repo *repo = repo_create(pool, "rubygems");
    if (argc < 2) {
        usage(argv[0]);
        return 1;
    }

    int i;
    for (i = 1; i < argc; ++i) {
        status = stat (argv[i], &st_buf);
        if (status != 0) {
            printf ("Error, errno = %d\n", errno);
            return 1;
        }
        else if (S_ISREG (st_buf.st_mode)) {
          ret = repo_add_rubygem(repo, argv[1], flags);
          if (ret != 0) {
            fprintf(stderr, "rubygems2solv: %s\n", pool_errstr(pool));
          }
        }
        else if (S_ISDIR (st_buf.st_mode)) {
          ret = repo_add_rubygem_dir(repo, argv[1], flags);
          if (ret != 0) {
            fprintf(stderr, "rubygems2solv: %s\n", pool_errstr(pool));
          }
        }
    }

    tool_write(repo, basefile, 0);

    pool_free(pool);
    return ret;
}