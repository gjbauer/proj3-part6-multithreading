#ifndef SUPERBLOCK_H
#define SUPERBLOCK_H

#include "disk.h"

// File system metadata (superblock)
typedef struct Superblock {
    uint64_t magic_number;           // File system identifier ✔
    uint64_t block_size;             // Size of each block ✔
    uint64_t total_blocks;           // Total blocks in filesystem ✔
    uint64_t free_blocks;            // Number of free blocks ✔
    uint64_t inode_bitmap;           // Location of start of inode bitmap ✔
    uint64_t root_inode;             // Root directory inode number ✔
    uint64_t btree_root;             // Root of B-tree index ✔
    uint64_t journal_start;          // First page of journal ✔
    uint64_t journal_head;           // Position of earliest journal entry ✔
    //uint64_t next_free_block;        // Next free block for allocation
    //uint64_t next_free_inode;        // Next available inode number
    char volume_name[32];            // Volume label ✔
    uint64_t creation_time;          // Filesystem creation timestamp ✔
    uint64_t last_mount_time;        // Last mount timestamp
} Superblock;

// Superblock operations
int superblock_read(DiskInterface* disk, cache *cache, Superblock* superblock);
int superblock_write(DiskInterface* disk, cache *cache, const Superblock* superblock, bool write_through);
int superblock_initialize(DiskInterface* disk, cache *cache, const char* volume_name);

// inode offsets
int calculate_inode_bitmap_size(Superblock *superblock);
int calculate_inode_table_size(Superblock *superblock);

// journal offset
int calculate_journal_size(Superblock *superblock);

#endif

