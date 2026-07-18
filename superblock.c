#include "cache.h"
#include "superblock.h"
#include <string.h>
#include "config.h"
#include <stdint.h>
#include "inode.h"
#include <time.h>
#include "lock.h"

// Superblock operations
int superblock_read(DiskInterface* disk, cache *cache, Superblock* superblock)
{
    block_type_t *block_type = (block_type_t*)get_block(disk, cache, 0, 0);
    if (*block_type != BLOCK_TYPE_SUPER) {
        fprintf(stderr, "ERROR: Not a valid superblock!\n");
        return -1;
    }
    memcpy(superblock, (Superblock*) ( block_type + 1 ), sizeof(Superblock));
    return 0;
}

int superblock_write(DiskInterface* disk, cache *cache, const Superblock* superblock, bool write_through)
{
    block_type_t *block_type = (block_type_t*)get_block(disk, cache, 0, 0);
    if (*block_type != BLOCK_TYPE_SUPER) return -1;
    memcpy( (Superblock*) ( block_type + 1 ), superblock, sizeof(struct Superblock));
    if (write_through)
    {
    	pthread_mutex_lock(get_lock());
        disk_write_block(disk, 0, block_type);
        pthread_mutex_unlock(get_lock());
        if (cache) decrease_pin_count(disk, cache, 0, 0);
    }
    else
    {
        write_block(disk, cache, block_type, 0, 0);
        if (cache) increase_pin_count(disk, cache, 0, 0);
    }
    return 0;
}

int superblock_initialize(DiskInterface* disk, cache *cache, const char* volume_name)
{
    if (strlen(volume_name) > 31) return -1;
    block_type_t *block_type = (block_type_t*)get_block(disk, cache, 0, 0);
    *block_type = BLOCK_TYPE_SUPER;
    Superblock* superblock = (Superblock*) ( block_type + 1 );

    printf("Size of superblock: %lu\n", sizeof(Superblock));
    superblock->magic_number = 0x53465452424e;
    superblock->block_size = BLOCK_SIZE;
    superblock->total_blocks = disk->total_blocks;
    superblock->free_blocks = disk->total_blocks;

    printf("Total blocks: %llu\n", superblock->total_blocks);
    uint32_t block_bitmap_space = (superblock->total_blocks % USABLE_BLOCK_SIZE) ? ( (superblock->total_blocks / USABLE_BLOCK_SIZE) + 1 ) : (superblock->total_blocks / USABLE_BLOCK_SIZE);
    printf("Number of blocks needed for block bitmap: %u\n", block_bitmap_space );
    printf("Number of blocks needed for inode bitmap: %d\n", calculate_inode_bitmap_size(superblock) );
    printf("Total number of blocks needed for bitmaps: %u\n", block_bitmap_space + calculate_inode_bitmap_size(superblock));
    printf("Blocks reserved for inodes: %u\n", calculate_inode_table_size(superblock));
    superblock->inode_bitmap = 1 + block_bitmap_space;

    strcpy(superblock->volume_name, volume_name);

    superblock->creation_time = time(NULL);
    return 0;
}

// inode offsets
int calculate_inode_bitmap_size(Superblock *superblock)
{
    return (superblock->total_blocks % (4*USABLE_BLOCK_SIZE)) ? ( (superblock->total_blocks / (4*USABLE_BLOCK_SIZE) ) + 1 ) : (superblock->total_blocks / (4*USABLE_BLOCK_SIZE) );
}

int calculate_inode_table_size(Superblock *superblock)
{
    return (superblock->total_blocks / 4) / (USABLE_BLOCK_SIZE/sizeof(Inode));
}

// journal offset

int calculate_journal_size(Superblock *superblock)
{
    return (superblock->total_blocks / 100);
}
