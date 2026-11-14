/* SPDX-License-Identifier: MIT
 * A disk held whole in memory (desktop tools and tests): a mirage_disk_t over a 450,560-byte image. */
#pragma once
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mirage.h"

typedef struct {
    mirage_disk_t disk;
    uint8_t *image;   /* MIRAGE_DISK_BYTES */
    int writes;
} disk_image_t;

static int disk_image_read(void *ctx, uint32_t off, uint8_t *dst, uint32_t n)
{
    disk_image_t *d = (disk_image_t *)ctx;
    if (off + n > MIRAGE_DISK_BYTES) return 1;
    memcpy(dst, d->image + off, n);
    return 0;
}

static int disk_image_write(void *ctx, uint32_t off, const uint8_t *src, uint32_t n)
{
    disk_image_t *d = (disk_image_t *)ctx;
    if (off + n > MIRAGE_DISK_BYTES) return 1;
    memcpy(d->image + off, src, n);
    d->writes++;
    return 0;
}

static void disk_image_init(disk_image_t *d, uint8_t *image)
{
    d->image = image;
    d->writes = 0;
    d->disk.read = disk_image_read;
    d->disk.write = disk_image_write;
    d->disk.ctx = d;
}

/* reads a whole file; returns its size, or -1 (and *out 0) */
static inline long read_file(const char *path, uint8_t **out)
{
    FILE *f = fopen(path, "rb");
    long n;
    *out = 0;
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    *out = (uint8_t *)malloc((size_t)n + 1);
    if (!*out || fread(*out, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(*out); *out = 0; return -1; }
    fclose(f);
    return n;
}
