
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>
#include <zlib.h>
#include <glob.h>
#include <errno.h>
#include <sys/stat.h>

#include <yaml.h>

#include <archive.h>
#include <archive_entry.h>

#include "rubygems_parser.h"

#define BLOCK_SIZE 16384
#define ZLIB_BUFFER_SIZE 64000
#define RUBYGEM_GZIP_HEADER_LEN 10

static void gem_parse_error(ParseContext *ctx, const char *format, ...)
{
    va_list args;
    if (ctx->gem_parse_error_callback) {
        va_start(args, format);
        char buffer[4096];
        vsnprintf(buffer, 4096, format, args);
        ctx->gem_parse_error_callback(ctx->data, buffer);
        va_end(args);
    }
}

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

static int parse_attribute(ParseContext *ctx, const char *attr, const char *val)
{
    if (ctx->gem_attr_callback)
        ctx->gem_attr_callback(ctx->data, attr, val);
    return 0;
}

static int parse_version(ParseContext *ctx, yaml_node_t *node)
{
    /*
      version: 0.4.1
    */
    const char *version = yaml_get_map_node_str(ctx->doc, node, "version");

    if (!version) {
      gem_parse_error(ctx, "Error parsing version");
      return -1;
    }
    parse_attribute(ctx, "version", version);
    return 0;
}

static int parse_requirement(ParseContext *ctx, const char *name, yaml_node_t *node_req)
{
    /*
    - ">="
    - !ruby/object:Gem::Version
      version: 1.0.5
    */
    if (node_req->type != YAML_SEQUENCE_NODE) {
        gem_parse_error(ctx, "Error parsing requirement");
        return -1;
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

    if (ctx->gem_dep_callback)
        ctx->gem_dep_callback(ctx->data, name, op, version);
}

static int parse_dependency(ParseContext *ctx, yaml_node_t *node)
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
        gem_parse_error(ctx, "Error parsing dependency");
        return -1;
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

static int parse_dependencies(ParseContext *ctx, yaml_node_t *node)
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
        gem_parse_error(ctx, "Error parsing deps");
        return -1;
    }

    if (ctx->gem_deps_start_callback)
        ctx->gem_deps_start_callback(ctx->data);

    yaml_node_item_t *i;
    for (i = node->data.sequence.items.start; i < node->data.sequence.items.top; ++i)
    {
        yaml_node_t *item = yaml_document_get_node(ctx->doc, *i);

        parse_dependency(ctx, item);
    }

    if (ctx->gem_deps_end_callback)
        ctx->gem_deps_end_callback(ctx->data);

    return 0;
}

static int parse_root_node(ParseContext *ctx, yaml_node_t *node)
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

static int gem_parse_metadata_entry(ParseContext *ctx, struct archive *a, struct archive_entry *entry)
{
    yaml_parser_t parser;
    yaml_document_t document;
    int ret = 0;

    unsigned char *metadata_gz;
    unsigned char *metadata;

    int metadata_gz_len = 0;
    int metadata_len = 0;

    metadata_gz = read_archive(a, &metadata_gz_len);
    if (!metadata_gz) {
        gem_parse_error(ctx, "Error reading gem archive: %s", archive_error_string(a));
        free(metadata_gz);
        return -1;
    }

    metadata = decompress(metadata_gz + RUBYGEM_GZIP_HEADER_LEN, metadata_gz_len - RUBYGEM_GZIP_HEADER_LEN, &metadata_len);
    free(metadata_gz);

    if (!metadata) {
        gem_parse_error(ctx, "Error decompressing (%d) -> (%d)", metadata_gz_len, metadata_len);
        free(metadata);
        return -1;
    }
    if (ctx->gem_yaml_metadata_callback) {
        ctx->gem_yaml_metadata_callback(ctx->data, metadata, metadata_len);
    }

    yaml_parser_initialize(&parser);
    yaml_parser_set_input_string(&parser, metadata, metadata_len);

    ret = !yaml_parser_load(&parser, &document);
    if (ret != 0) {
        gem_parse_error(ctx, "Error parsing YAML document");
        free(metadata);
        yaml_parser_delete(&parser);
        yaml_document_delete(&document);
        return -1;
    }

    yaml_node_t *root = yaml_document_get_root_node(&document);
    yaml_parser_delete(&parser);
    if (!root) {
        gem_parse_error(ctx, "Error getting YAML document root node");
        yaml_document_delete(&document);
        return -1;
    }

    ctx->doc = &document;
    memset(&ctx->jd, 0, sizeof(ctx->jd));

    ret = parse_root_node(ctx, root);
    yaml_document_delete(&document);

    free(metadata);

    if (ret != 0) {
        gem_parse_error(ctx, "Error parsing YAML document");
    }
    // start new gem callback
    if (ctx->gem_end_callback)
        ctx->gem_end_callback(ctx->data);

    return ret;
}


int gem_parse_add_rubygem(ParseContext *ctx, const char *rubygem)
{
    struct archive *a;
    struct archive_entry *entry;
    int ret;

    // start new gem callback
    if (ctx->gem_start_callback)
        ctx->gem_start_callback(ctx->data, rubygem);

    a = archive_read_new();
    archive_read_support_compression_gzip(a);
    archive_read_support_format_tar(a);

    ret = archive_read_open_filename(a, rubygem, BLOCK_SIZE);
    if (ret != ARCHIVE_OK) goto out;

    ret = -1;
    while (archive_read_next_header(a, &entry) == ARCHIVE_OK) {
        if (!strcmp(archive_entry_pathname(entry), "metadata.gz"))
            ret = gem_parse_metadata_entry(ctx, a, entry);
        else
          archive_read_data_skip(a);
    }
out:
    if (ret != 0) {
      gem_parse_error(ctx, "Error reading gem file %s: %s", rubygem, archive_error_string(a));
    }
    archive_read_finish(a);

    // end rubygem callback

    return ret;
}

int gem_parse_add_rubygem_dir(ParseContext *ctx, const char *dir)
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
            gem_parse_error(ctx, "out of memory");
            return -1;
        case GLOB_ABORTED:
            gem_parse_error(ctx, "reading error");
            return -1;
        case GLOB_NOMATCH:
            gem_parse_error(ctx, "no files found");
            return -1;
        default:
            break;
    }

    int i;
    for(i=0; i<data.gl_pathc; i++)
    {
        if (gem_parse_add_rubygem(ctx, data.gl_pathv[i]) != 0)
          ret = -1;
    }

    globfree( &data );
    return ret;
}

void gem_parse_context_initialize(ParseContext *ctx)
{
    memset(ctx, 0, sizeof(ParseContext));
}

void gem_parse_context_free(ParseContext *ctx)
{
    join_freemem(&ctx->jd);
}

int gem_parse(ParseContext *ctx, int argc, char **locations)
{
    int status;
    struct stat st_buf;
    int ret;

    if (ctx->gem_parse_start_callback)
        ctx->gem_parse_start_callback(ctx->data);

    int i;
    for (i = 0; i < argc; ++i) {
        status = stat (locations[i], &st_buf);
        if (status != 0) {
            printf ("Error, errno = %d\n", errno);
            return 1;
        }
        else if (S_ISREG (st_buf.st_mode)) {
          ret = gem_parse_add_rubygem(ctx, locations[i]);
          if (ret != 0) {
            gem_parse_error(ctx, "Error parsing %s", locations[i]);
          }
        }
        else if (S_ISDIR (st_buf.st_mode)) {
          ret = gem_parse_add_rubygem_dir(ctx, locations[i]);
          if (ret != 0) {
            gem_parse_error(ctx, "Error parsing %s", locations[i]);
          }
        }
    }

    if (ctx->gem_end_callback)
        ctx->gem_parse_end_callback(ctx->data);

    return 0;
}