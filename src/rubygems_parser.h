
#ifndef RUBYGEMS_PARSER_H
#define RUBYGEMS_PARSER_H

#include <yaml.h>

/* we can get rid of this if we implement our own join2 */
#include <solv/pool.h>
#include <solv/repo.h>
#include <stdlib.h>
#include "tools_util.h"

typedef struct
{
    yaml_document_t *doc;
    struct joindata jd;

    /* start of all parsing */
    int (*gem_parse_start_callback)(void *user_data);
    /* start of a gem */
    int (*gem_start_callback)(void *user_data, const char *filename);
    /* the full yaml metadata */
    int (*gem_yaml_metadata_callback)(void *user_data, const char *buff, int len);
    /* an attribute */
    int (*gem_attr_callback)(void *user_data, const char*attr, const char *val);
    int (*gem_deps_start_callback)(void *user_data);
    int (*gem_dep_callback)(void *user_data, const char *name, const char *op, const char *version);
    int (*gem_deps_end_callback)(void *user_data);
    int (*gem_end_callback)(void *user_data);
    int (*gem_parse_end_callback)(void *user_data);
    void (*gem_parse_error_callback)(void *user_data, const char *msg);

    void *data;
} ParseContext;

void gem_parse_context_initialize(ParseContext *ctx);
int gem_parse(ParseContext *ctx, int argc, char **locations);


#endif
