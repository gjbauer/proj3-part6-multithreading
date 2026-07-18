#include "directory.h"
#include "hash.h"
#include "cache.h"
#include "inode.h"
#include "btr.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "journal.h"
#include "lock.h"

int directory_add_entry(DiskInterface* disk, cache *cache, const char *path, const char* name, uint64_t target_inode, FileType type, bool write_through)
{
    int rv = -1;
    InodeBtreePair *pair = item_search(disk, cache, path);
    Inode node = {0};
    uint64_t block;
    uint16_t count = 0;
    uint16_t number_of_entries;
    block_type_t *block_type;
    DirectoryBlock *db;
    DirEntry *entry;
    DirEntry new_file;
    
    new_file.inode_number = target_inode;
    strcpy(new_file.name, name);
    new_file.active = true;
    
    if (pair->inode_number || !strcmp(path, "/"))
    {
        inode_read(disk, cache, pair->inode_number, &node);
        for (uint16_t i=0; i < UINT16_MAX; i++)
        {
            block = 0;
            inode_get_block(disk, cache, &node, i, &block);
            if (!block)
            {
                block = alloc_page(disk, cache);
                if (!block) goto free_pair;
                inode_set_block(disk, cache, &node, i, block);
                block_type = get_block(disk, cache, pair->inode_number, block);
                *block_type = BLOCK_TYPE_DATA;
                inode_write(disk, cache, &node, write_through);
                journal_entry_t entry;
                entry.type = WRITE;
                entry.write.inode_number = node.inode_number;
                entry.write.block_index = i;
                entry.write.physical_block = block;
                printf("WRITE to directory block # %llu\n", block);
                initialize_journal_entry(disk, cache, &entry);
                if (0 == i)
                {
                    db = (DirectoryBlock*) ( block_type + 1 );
                    db->entry_count = 0;
                }
                write_block(disk, cache, block_type, 0, block);
            }
            else block_type = get_block(disk, cache, pair->inode_number, block);
            
            if (BLOCK_TYPE_DATA != *block_type)
            {
                fprintf(stderr, "ERROR: Not a data type block!!\n");
                rv = -1;
                goto free_pair;
            }
            
            if (0 == i)
            {
                db = (DirectoryBlock*) ( block_type + 1 );
                entry = (DirEntry*) ( db + 1 );
                number_of_entries = db->entry_count;
            }
            else entry = (DirEntry*) ( block_type + 1 );
            
            uint16_t entries_per_block;
            if (0 == i) {
                // First block has DirectoryBlock header
                entries_per_block = (USABLE_BLOCK_SIZE-sizeof(struct DirectoryBlock)) / sizeof(struct DirEntry);
            } else {
                // Subsequent blocks are just entries
                entries_per_block = USABLE_BLOCK_SIZE / sizeof(struct DirEntry);
            }
            for (uint16_t j=0; j < entries_per_block - 1 ; j++)
            {
                if (!entry[j].active || count == number_of_entries)
                {
                    number_of_entries++;
                    printf("Adding entry: name='%s', inode=%llu, type=%d, count before=%d, count after=%d, block=%llu\n",
                           name, target_inode, type, number_of_entries - 1, number_of_entries, block);
                    if (FILE_TYPE_DIRECTORY == type)
                    {
                        uint64_t dir_root_page;
                        BTreeNode *dir_root = btree_node_create(disk, cache, false, &dir_root_page);
                        dir_root->value = target_inode;
                        dir_root->type = FILE_TYPE_DIRECTORY;
                        // Initialize as an empty internal node ready for children
                        btree_node_write(disk, cache, dir_root);
                        
                        // Insert this root node into the parent's B-tree
                        if (btree_insert(disk, cache, pair->btree_block, path_hash(name), dir_root_page, type) != 0) {
                            // B-tree insertion failed, clean up
                            btree_node_free(disk, cache, dir_root_page);
                            rv = -1;
                            goto free_pair;
                        }
                        new_file.btree_block = dir_root_page;
                    }
                    else {
                        if (btree_insert(disk, cache, pair->btree_block, path_hash(name), target_inode, type) != 0) {
                            // B-tree insertion failed
                            rv = -1;
                            goto free_pair;
                        }
                    }
                    memcpy( &entry[j], &new_file, sizeof(struct DirEntry) );
                    pthread_mutex_lock(get_lock());
                    disk_write_block(disk, block, block_type);
                    pthread_mutex_unlock(get_lock());
                    inode_get_block(disk, cache, &node, 0, &block);
                    block_type = get_block(disk, cache, pair->inode_number, block);
                    db = (DirectoryBlock*) ( block_type + 1 );
                    db->entry_count = number_of_entries;
                    pthread_mutex_lock(get_lock());
                    disk_write_block(disk, block, block_type);
                    pthread_mutex_unlock(get_lock());
                    write_block(disk, cache, block_type, 0, block);
                    btree_write(disk, cache, pair->btree_block);
                    rv = 0;
                    goto free_pair;
                }
                if (entry[j].active) count++;
            }
        }
    }
free_pair:
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    free(pair);
    return rv;
}

int directory_remove_entry(DiskInterface* disk, cache *cache, const char *path, const char* name, bool write_through)
{
    int rv = -ENOENT;
    InodeBtreePair *pair = item_search(disk, cache, path);
    Inode dir_node = {0}, file_node = {0};
    uint64_t block;
    int16_t number_of_entries;
    block_type_t *block_type;
    DirectoryBlock *db;
    DirEntry *entry;
    
    uint16_t count = 0;
    
    if (pair->inode_number || !strcmp(path, "/"))
    {
        inode_read(disk, cache, pair->inode_number, &dir_node);
        for (uint16_t i=0; i < UINT16_MAX; i++)
        {
            block = 0;
            inode_get_block(disk, cache, &dir_node, i, &block);
            if (!block)
                goto free_pair;
            block_type = get_block(disk, cache, pair->inode_number, block);
            if (BLOCK_TYPE_DATA != *block_type)
            {
                fprintf(stderr, "ERROR: Not a data type block!!\n");
                goto free_pair;
            }
            if (0 == i)
            {
                db = (DirectoryBlock*) ( block_type + 1 );
                entry = (DirEntry*) ( db + 1 );
                number_of_entries = db->entry_count;
            }
            else entry = (DirEntry*) ( block_type + 1 );
            if (number_of_entries == count) break;
            uint16_t entries_per_block = (USABLE_BLOCK_SIZE - sizeof(struct DirectoryBlock)) / sizeof(struct DirEntry);
            for (uint16_t j=0; j < entries_per_block - 1 ; j++)
            {
                if (!strcmp(entry->name, name))
                {
                    entry->active = false;
                    number_of_entries--;
                    inode_read(disk, cache, entry->inode_number, &file_node);
                    if (1 == file_node.reference_count)
                    {
                        if (inode_free(disk, cache, entry->inode_number, write_through))
                            goto free_pair;
                        uint64_t tree_node_block = btree_search(disk, cache, pair->btree_block, path_hash(name));
                        if (tree_node_block)
                        {
                        	btree_delete(disk, cache, pair->btree_block, path_hash(name));
                        	btree_write(disk, cache, pair->btree_block);
                        }
                        btree_print(disk, cache, pair->btree_block , 0);
                        if (write_through)
                        	btree_write(disk, cache, pair->btree_block);
                    }
                    pthread_mutex_lock(get_lock());
                    disk_write_block(disk, block, block_type);
                    pthread_mutex_unlock(get_lock());
                    inode_get_block(disk, cache, &dir_node, 0, &block);
                    block_type = get_block(disk, cache, pair->inode_number, block);
                    db = (DirectoryBlock*) ( block_type + 1 );
                    db->entry_count = number_of_entries;
                    if (write_through)
                    {
                    	pthread_mutex_lock(get_lock());
                        disk_write_block(disk, block, block_type);
                        pthread_mutex_unlock(get_lock());
                        decrease_pin_count(disk, cache, pair->inode_number, block);
                    }
                    else
                    {
                        write_block(disk, cache, block_type, pair->inode_number, block);
                        increase_pin_count(disk, cache, pair->inode_number, block);
                    }
                    rv = 0;
                    break;
                }
                entry++;
                if (entry->active) count++;
            }
        }
    }
free_pair:
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    free(pair);
    return rv;
}

int directory_list(DiskInterface* disk, cache *cache, const char *path, DirEntry** entries)
{
    int rv = 0;
    InodeBtreePair *pair = item_search(disk, cache, path);
    Inode node = {0};
    uint64_t block;
    int16_t number_of_entries;
    block_type_t *block_type;
    DirectoryBlock *db;
    DirEntry *entry;
    
    if (pair->inode_number || !strcmp(path, "/"))
    {
        inode_read(disk, cache, pair->inode_number, &node);
        for (uint16_t i=0; i < UINT16_MAX; i++)
        {
            inode_get_block(disk, cache, &node, i, &block);
            printf("Getting block number: %llu\n", block);
            if (!block)
                break;
            block_type = get_block(disk, cache, pair->inode_number, block);
            if (BLOCK_TYPE_DATA != *block_type)
            {
                fprintf(stderr, "ERROR: Not a data type block!!\n");
                break;
            }
            if (0 == i)
            {
                db = (DirectoryBlock*) ( block_type + 1 );
                *entries = malloc( db->entry_count * sizeof(struct DirEntry) );
                entry = (DirEntry*) ( db + 1 );
                number_of_entries = db->entry_count;
            }
            else entry = (DirEntry*) ( block_type + 1 );
            if (db->entry_count == rv) break;
            uint16_t entries_per_block = USABLE_BLOCK_SIZE / sizeof(struct DirEntry);
            for (uint16_t j=0; j < entries_per_block && rv < db->entry_count; j++, entry++)
            {
                if (entry->active)
                {
                    memcpy( &(*entries)[rv], entry, sizeof(struct DirEntry) );
                    rv++;
                }
            }
        }
    }
    btree_print(disk, cache, pair->btree_block, 0);
    printf("=== directory_list for %s ===\n", path);
    for (int i = 0; i < rv; i++) {
        printf("  entry %d: name='%s', inode=%llu, active=%d\n",
               i, (*entries)[i].name, (*entries)[i].inode_number, (*entries)[i].active);
    }
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    free(pair);
    return rv;
}

bool directory_exists_entry(DiskInterface* disk, cache *cache, const char *path, const char* name, DirEntry *stack_entry)
{
    bool rv = false;
    InodeBtreePair *pair = item_search(disk, cache, path);
    Inode dir_node = {0};
    uint64_t block;
    int16_t number_of_entries;
    block_type_t *block_type;
    DirectoryBlock *db;
    DirEntry *entry;
    
    uint16_t count = 0;
    
    if (pair->inode_number || !strcmp(path, "/"))
    {
        inode_read(disk, cache, pair->inode_number, &dir_node);
        for (uint16_t i=0; i < UINT16_MAX; i++)
        {
            block = 0;
            inode_get_block(disk, cache, &dir_node, i, &block);
            if (!block)
                goto free_pair;
            block_type = get_block(disk, cache, pair->inode_number, block);
            if (BLOCK_TYPE_DATA != *block_type)
            {
                fprintf(stderr, "ERROR: Not a data type block!!\n");
                goto free_pair;
            }
            if (0 == i)
            {
                db = (DirectoryBlock*) ( block_type + 1 );
                entry = (DirEntry*) ( db + 1 );
                number_of_entries = db->entry_count;
            }
            else entry = (DirEntry*) ( block_type + 1 );
            if (number_of_entries == count) break;
            uint16_t entries_per_block = (USABLE_BLOCK_SIZE - sizeof(struct DirectoryBlock)) / sizeof(struct DirEntry);
            for (uint16_t j=0; j < entries_per_block - 1 ; j++)
            {
                if (!strcmp(entry->name, name) && entry->active)
                {
                    if (stack_entry != NULL) memcpy(stack_entry, entry, sizeof(struct DirEntry));
                    rv = true;
                    goto free_pair;
                }
                entry++;
                if (entry->active) count++;
            }
        }
    }
free_pair:
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    free(pair);
    return rv;
}
