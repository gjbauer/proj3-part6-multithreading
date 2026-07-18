#ifndef METADATA_API_H
#define METADATA_API_H

#include "inode.h"
#include "hash.h"
#include <time.h>
#include "directory.h"
#ifdef __linux__
#include <bsd/stdlib.h>
#endif
#include <stdlib.h>
#include <stdio.h>
#include "disk.h"
#include "cache.h"
#include "string.h"
#include <stdint.h>

int _mknod(DiskInterface *disk, cache *cache, const char *path, mode_t mode, uint64_t btree_block, bool write_through, int64_t *out_inode);

int _unlink(DiskInterface *disk, cache *cache, const char *path, bool write_through);

int _link(DiskInterface *disk, cache *cache, const char *from, const char *to, bool write_through);

int _chmod(DiskInterface *disk, cache *cache, const char *path, mode_t mode, bool write_through);

int _truncate(DiskInterface *disk, cache *cache, const char *path, off_t size, bool write_through);

int _rename(DiskInterface *disk, cache *cache, const char *from, const char *to, bool write_through);
#endif
