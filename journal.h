#ifndef JOURNAL_H
#define JOURNAL_H
#include "disk.h"
#include "cache.h"
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

#undef PATH_MAX
#define PATH_MAX 2030
#undef NAME_MAX
#define NAME_MAX 256

typedef enum transaction_type_t
{
    UNINITIALIZED = 0x0,
    MKNOD = 0x444e4b4d,
    UNLINK = 0x4b4e4c55,
    LINK = 0x4b4e494c,
    CHMOD = 0x444d4843,
    TRUNCATE = 0x54435254,
    WRITE = 0x54495257,
    RENAME = 0x4d4e4552,
} transaction_type_t;

typedef struct journal_entry_t
{
    transaction_type_t type;
    uint64_t block_number;
    bool synced;
    union
    {
        struct
        {
            char path[PATH_MAX];
            mode_t mode;
            uint64_t btree_block;
            int64_t inode_number;
        } mknod;
        struct
        {
            char path[PATH_MAX];
        } unlink;
        struct
        {
            char from[PATH_MAX];
            char to[PATH_MAX];
        } link;
        struct
        {
            char path[PATH_MAX];
            mode_t mode;
        } chmod;
        struct
        {
            char path[PATH_MAX];
            off_t size;
        } truncate;
        struct
        {
            int64_t inode_number;
            uint64_t block_index;
            uint64_t physical_block;
            size_t  size;
        } write;
        struct
        {
            char from[PATH_MAX];
            char to[PATH_MAX];
        } rename;
    };
} journal_entry_t;

void initialize_journal_entry(DiskInterface *disk, cache *cache, journal_entry_t *entry);

void sync_entry(DiskInterface *disk, cache *cache, journal_entry_t *entry);

void sync_journal(DiskInterface *disk, cache *cache);

void mark_journal_synced(DiskInterface *disk, cache *cache);

#endif
