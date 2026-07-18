#ifndef DIRECTORY_H
#define DIRECTORY_H

#include "config.h"
#include "disk.h"
#include "journal.h"
#include "types.h"

// Directory entry structure
typedef struct DirEntry {
    uint64_t inode_number;           // Inode number of the entry
    uint64_t btree_block;            // B-Tree Block Number (if a directory)
    char name[NAME_MAX];             // Name of the entry
    bool active;                     // Is this directory entry active?
} DirEntry;

// Directory block structure
typedef struct DirectoryBlock {
    uint16_t entry_count;            // Number of entries in this directory
} DirectoryBlock;

// Directory operations
int directory_add_entry(DiskInterface* disk, cache *cache, const char *path, const char* name, uint64_t target_inode, FileType type, bool write_through);
int directory_remove_entry(DiskInterface* disk, cache *cache, const char *path, const char* name, bool write_through);
bool directory_exists_entry(DiskInterface* disk, cache *cache, const char *path, const char* name, DirEntry *stack_entry);
int directory_list(DiskInterface* disk, cache *cache, const char *path, DirEntry** entries);

#endif

