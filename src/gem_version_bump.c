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

#include <string.h>

char * gem_version_bump(const char *version)
{
    char *buffer;
    buffer = strdup(version);
    if (!buffer) return 0;

    char *s = buffer + strlen(buffer);
    for (; *s != '.'; --s);
    *s = '\0';
    s--;

    for (; s >= buffer && *s != '.' ; s--) {
        if (*s == '9') {
            *s = '0';
        }
        else {
            *s = (*s)++;
            break;
        }
    }

    if (*s == '.' && *(s + 1) == '0') {
        /* last one was a 9, move all to the right */
        for (; *s; s++);
        s++;
        *(s + 1) = '\0';
        for (; s > buffer && *(s - 1) != '.'; s--) {
            *s = *(s - 1);
        }
        *s = '1';
    }
    return buffer;
}
