#ifndef HASH_H
#define HASH_H
#include <stdint.h>
#include "disk.h"
#include "cache.h"

/**
 * Hash function utilities for filesystem operations
 */

/**
 * Compute FNV-1a hash of a file path string
 * Used for efficient path lookups and directory operations
 * @param path Null-terminated path string to hash
 * @return 64-bit hash value
 */
uint64_t path_hash(const char *path);

typedef struct InodeBtreePair
{
    uint64_t inode_number;
    uint64_t btree_block;
} InodeBtreePair;

/**
 * This function takes a given absolute path and returns the corresponding
 * inode and, if a directory, new B-Tree root as found in the B-Tree search process.
 */
InodeBtreePair * item_search(DiskInterface* disk, cache *cache, const char *path);

void
print_pair(InodeBtreePair *pair);

#endif
