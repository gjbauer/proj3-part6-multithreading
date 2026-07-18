#include "inode.h"
#include "superblock.h"
#ifdef __linux__
#include <bsd/stdlib.h>
#else
#include <stdlib.h>
#endif
#include <string.h>
#include "disk.h"
#include "lock.h"

int inode_read(DiskInterface* disk, cache *cache, uint64_t inode_number, Inode* inode)
{
    int rv = -1;
    Superblock sb;
    superblock_read(disk, cache, &sb);
    int inode_per_page = USABLE_BLOCK_SIZE / sizeof(Inode);
    uint64_t inode_page = inode_number / inode_per_page;
    block_type_t *block_type = get_block(disk, cache, 0, sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page);
    if (*block_type != BLOCK_TYPE_INODE)
    {
        fprintf(stderr, "ERROR: Not a valid inode table block!\n");
        goto clear_stack;
    }
    Inode *node = (Inode*) ( block_type + 1);
    node = node + ( inode_number % inode_per_page );
    
    memcpy(inode, node, sizeof(struct Inode));
    
    rv = 0;
    printf("inode_read: inode=%llu, mode=%o\n", inode_number, inode->mode);
clear_stack:
    arc4random_buf(&sb, sizeof(struct Superblock));
    return rv;
}

int inode_write(DiskInterface* disk, cache *cache, const Inode* inode, bool write_through)
{
    int rv = -1;
    Superblock sb;
    superblock_read(disk, cache, &sb);
    int inode_per_page = USABLE_BLOCK_SIZE / sizeof(Inode);
    uint64_t inode_page = inode->inode_number / inode_per_page;
    block_type_t *block_type = get_block(disk, cache, 0, sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page);
    if (*block_type != BLOCK_TYPE_INODE)
    {
        fprintf(stderr, "ERROR: Not a valid inode table block!\n");
        arc4random_buf(&sb, sizeof(struct Superblock));
        goto clear_stack;
    }
    Inode *node = (Inode*) ( block_type + 1);
    node = node + ( inode->inode_number % inode_per_page );
    memcpy(node, inode, sizeof(struct Inode));
    if (write_through)
    {
    	pthread_mutex_lock(get_lock());
        disk_write_block(disk, sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page, block_type);
        pthread_mutex_unlock(get_lock());
        decrease_pin_count(disk, cache, 0, sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page);
    }
    else
    {
        write_block(disk, cache, block_type, 0, sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page);
        increase_pin_count(disk, cache, 0, sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page);
    }
    rv = 0;
    printf("inode_write: inode=%llu, mode=%o\n", inode->inode_number, inode->mode);
clear_stack:
    arc4random_buf(&sb, sizeof(struct Superblock));
    return rv;
}

int64_t inode_allocate(DiskInterface* disk, cache *cache, mode_t mode, bool write_through)
{
    Superblock sb;
    Inode node;
    superblock_read(disk, cache, &sb);
    int ibmn = sb.inode_bitmap;
	void* ibm = get_block(disk, cache, 0, ibmn);
    block_type_t *block_type = (block_type_t*) ibm;
    int64_t rv = -1;

	// Search through all inodes to find first free one
	for (uint64_t ii = 0; BLOCK_TYPE_BITMAP == *block_type; ++ii) {
		if ( !(ii % USABLE_BLOCK_SIZE) && ii )
		{
			ibmn++;
			ibm = get_block(disk, cache, 0, ibmn);
            block_type = (block_type_t*) ibm;
		}
		if (!bitmap_get(ibm, ii - ((ibmn - sb.inode_bitmap) * USABLE_BLOCK_SIZE))) {  // Found a free inode
			if (bitmap_put(ibm, ii - ((ibmn - sb.inode_bitmap) * USABLE_BLOCK_SIZE), 1))  // Mark it as allocated
			{
				fprintf(stderr, "ERROR: Could not allocate inode!!\n");
                goto wipe_superblock;
			}
            if (inode_read(disk, cache, ii, &node))
            {
                fprintf(stderr, "ERROR: Could not read inode!!\n");
                goto wipe_inode;
            }
            node.inode_number = ii;
            node.mode = mode;
            node.owner_id = getuid();
            node.group_id = getgid();
            node.reference_count = 1;
            if (inode_write(disk, cache, &node, write_through))
            {
                fprintf(stderr, "ERROR: Could not write inode!!\n");
                goto wipe_inode;
            }
            if (write_through)
            {
            	pthread_mutex_lock(get_lock());
                disk_write_block(disk, ibmn, ibm);
                pthread_mutex_unlock(get_lock());
                decrease_pin_count(disk, cache, 0, ibmn);
            }
            else
            {
                write_block(disk, cache, ibm, 0, ibmn);
                increase_pin_count(disk, cache, 0, ibmn);
            }
			printf("+ inode_allocate() -> %llu\n", ii);
			rv = ii;
            goto wipe_inode;
		}
	}

    fprintf(stderr, "ERROR: No free inodes available for allocation!\n");
wipe_inode:
    arc4random_buf(&node, sizeof(struct Inode));
wipe_superblock:
    arc4random_buf(&sb, sizeof(struct Superblock));
	return rv;  // No free blocks available
}

int inode_free(DiskInterface* disk, cache *cache, uint64_t inode_number, bool write_through)
{
    int rv = -1;
    Superblock sb;
    superblock_read(disk, cache, &sb);
    
    // Calculate which bitmap block and bit offset
    uint64_t bitmap_start = sb.inode_bitmap;
    uint64_t bitmap_block_index = inode_number / USABLE_BLOCK_SIZE;  // Which bitmap block
    uint64_t bit_offset_in_block = inode_number % USABLE_BLOCK_SIZE; // Which bit within the block
    
    uint64_t ibmn = bitmap_start + bitmap_block_index;
    void* ibm = get_block(disk, cache, 0, ibmn);
    
    if (bitmap_put(ibm, bit_offset_in_block, 0))  // Mark bit as free
    {
        fprintf(stderr, "ERROR: Selected inode could not be freed!\n");
        goto return_rv;
    }
    // Securely erase inode contents
    int inode_per_page = USABLE_BLOCK_SIZE / sizeof(Inode);
    int inode_page = inode_number / inode_per_page;
    block_type_t *block_type = get_block(disk, cache, 0, ( sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page ));
    if (BLOCK_TYPE_INODE != *block_type)
    {
        fprintf(stderr, "ERROR: Not a valid inode block!!\n");
        goto return_rv;
    }
    Inode *node = ( (Inode*)( block_type + 1) + ( inode_number % inode_per_page ) );
    // Use memset(0) because we want our inodes to contain 0 data upon initialization
    memset(node, 0, sizeof(struct Inode) );
    if (write_through)
    {
    	pthread_mutex_lock(get_lock());
        disk_write_block(disk, ( sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page ), block_type);
        pthread_mutex_unlock(get_lock());
        decrease_pin_count(disk, cache, 0, ( sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page ));
    }
    else
    {
        write_block(disk, cache, block_type, 0,  ( sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page ) );
        increase_pin_count(disk, cache, 0, ( sb.inode_bitmap + calculate_inode_bitmap_size(&sb) + inode_page ));
    }
    printf("+ inode_free(%llu)\n", inode_number);
    if (write_through)
    {
    	pthread_mutex_lock(get_lock());
        disk_write_block(disk, ibmn, ibm);
        pthread_mutex_unlock(get_lock());
        decrease_pin_count(disk, cache, 0, ibmn);
    }
    else
    {
        write_block(disk, cache, ibm, 0, ibmn);
        increase_pin_count(disk, cache, 0, ibmn);
    }
    arc4random_buf(&sb, sizeof(struct Superblock));
    rv = 0;
return_rv:
    return 0;
}

int inode_get_block(DiskInterface* disk, cache *cache, Inode* inode, uint64_t block_index, uint64_t* physical_block)
{
    int rv = -1;
    if (block_index < 12)
    {
        *physical_block = inode->direct_blocks[block_index];
        if ( inode->direct_blocks[block_index] != 0 ) rv = 0;
    }
    else if (block_index < ( USABLE_BLOCK_SIZE / sizeof(uint64_t) ) )
    {
        if (!inode->indirect_block)
        {
            return rv;
        }
        else
        {
            block_type_t *block_type = get_block(disk, cache, inode->inode_number, inode->indirect_block);
            if (BLOCK_TYPE_DATA != *block_type) return -1;
            uint64_t *sind = (uint64_t*)( block_type + 1 );
            sind += ( block_index - 12 );
            *physical_block = *sind;
            rv = 0;
        }
    }
    else
    {
    	int double_block_index = block_index -  ( ( USABLE_BLOCK_SIZE / sizeof(uint64_t) ) + 12 );
        int double_block_page = double_block_index / ( USABLE_BLOCK_SIZE / sizeof(uint64_t) );
        int page_index = double_block_index % ( USABLE_BLOCK_SIZE / sizeof(uint64_t) );
        
    	if (!inode->double_indirect_block)
        {
            return rv;
        }
        
        block_type_t *block_type = get_block(disk, cache, inode->inode_number, inode->double_indirect_block);
        uint64_t *dind = (uint64_t*)( block_type + 1 );
        dind += double_block_page;
        
        block_type = get_block(disk, cache, inode->inode_number, *dind);
        dind = (uint64_t*)( block_type + 1 );
        dind += page_index;
        
        if (!*dind)
        {
            return rv;
        }
        
        *physical_block = *dind;
        rv = 0;
    }
    return rv;
}

int inode_set_block(DiskInterface* disk, cache *cache, Inode* inode, uint64_t block_index, uint64_t physical_block)
{
    int rv = -1;
    if (block_index < 12)
    {
        inode->direct_blocks[block_index] = physical_block;
        inode_write(disk, cache, &(*inode), true);
        rv = 0;
    }
    else if (block_index <= ( USABLE_BLOCK_SIZE / sizeof(uint64_t) ) + 12 )
    {
    	if ( block_index == 12 && physical_block == 0)
    	{
    		if (inode->indirect_block) free_page(disk, cache, inode->indirect_block);
    	}
    	
        if (!inode->indirect_block)
        {
            inode->indirect_block = alloc_page(disk, cache);
            if (!inode->indirect_block) return rv;
            block_type_t *block_type = get_block(disk, cache, inode->inode_number, inode->indirect_block);
            *block_type = BLOCK_TYPE_DATA;
            inode_write(disk, cache, &(*inode), true);
            inode_read(disk, cache, inode->inode_number, &(*inode));
            uint64_t *sind = (uint64_t*)( block_type + 1 );
            memset(sind, 0, USABLE_BLOCK_SIZE);
            write_block(disk, cache, block_type, inode->inode_number, inode->indirect_block);
            #ifndef CACHE_DISABLED
            pthread_mutex_lock(get_lock());
            disk_write_block(disk, inode->indirect_block, block_type);
            pthread_mutex_unlock(get_lock());
            #endif
        }
        
        // Now set the specific block pointer
        block_type_t *block_type = get_block(disk, cache, inode->inode_number, inode->indirect_block);
        uint64_t *sind = (uint64_t*)( block_type + 1 );
        sind += ( block_index - 12 );
        *sind = physical_block;
        write_block(disk, cache, block_type, inode->inode_number, inode->indirect_block);
        #ifndef CACHE_DISABLED
        pthread_mutex_lock(get_lock());
        disk_write_block(disk, inode->indirect_block, block_type);
        pthread_mutex_unlock(get_lock());
        #endif
        block_type = get_block(disk, cache, inode->inode_number, physical_block);
        *block_type = BLOCK_TYPE_DATA;
        write_block(disk, cache, block_type, inode->inode_number, physical_block);
        #ifndef CACHE_DISABLED
        pthread_mutex_lock(get_lock());
        disk_write_block(disk, physical_block, block_type);
        pthread_mutex_unlock(get_lock());
        #endif
        rv = 0;
    }
    else
    {
    	int double_block_index = block_index -  ( ( USABLE_BLOCK_SIZE / sizeof(uint64_t) ) + 12 );
        int double_block_page = double_block_index / ( USABLE_BLOCK_SIZE / sizeof(uint64_t) );
        int page_index = double_block_index % ( USABLE_BLOCK_SIZE / sizeof(uint64_t) );
        
        if ( page_index == 0 && physical_block == 0)
    	{
    		if (inode->double_indirect_block)
        	{
    			block_type_t *block_type = get_block(disk, cache, inode->inode_number, inode->double_indirect_block);
        		uint64_t *dind = (uint64_t*)( block_type + 1 );
        		dind += double_block_page;
    			if (*dind) free_page(disk, cache, *dind);
    		}
    		if (double_block_page == 0) free_page(disk, cache, inode->double_indirect_block);
    	}
        
    	if (!inode->double_indirect_block)
        {
            inode->double_indirect_block = alloc_page(disk, cache);
            if (!inode->double_indirect_block) return rv;
            block_type_t *block_type = get_block(disk, cache, inode->inode_number, inode->double_indirect_block);
            *block_type = BLOCK_TYPE_DATA;
            inode_write(disk, cache, &(*inode), true);
            inode_read(disk, cache, inode->inode_number, &(*inode));
            uint64_t *sind = (uint64_t*)( block_type + 1 );
            memset(sind, 0, USABLE_BLOCK_SIZE);
            write_block(disk, cache, block_type, inode->inode_number, inode->indirect_block);
            #ifndef CACHE_DISABLED
            pthread_mutex_lock(get_lock());
            disk_write_block(disk, inode->indirect_block, block_type);
            pthread_mutex_unlock(get_lock());
            #endif
        }
        
        block_type_t *block_type = get_block(disk, cache, inode->inode_number, inode->double_indirect_block);
        uint64_t *dind = (uint64_t*)( block_type + 1 );
        dind += double_block_page;
        if (!*dind)
        {
            *dind = alloc_page(disk, cache);
            if (!*dind) return rv;
            block_type = get_block(disk, cache, inode->inode_number, *dind);
            *block_type = BLOCK_TYPE_DATA;
            write_block(disk, cache, block_type, inode->inode_number, *dind);
            #ifndef CACHE_DISABLED
            pthread_mutex_lock(get_lock());
            disk_write_block(disk, inode->indirect_block, block_type);
            pthread_mutex_unlock(get_lock());
            #endif
        }
        
        block_type = get_block(disk, cache, inode->inode_number, *dind);
        dind = (uint64_t*)( block_type + 1 );
        dind += page_index;
        
        *dind = physical_block;
        write_block(disk, cache, block_type, inode->inode_number, *dind);
        #ifndef CACHE_DISABLED
        pthread_mutex_lock(get_lock());
        disk_write_block(disk, inode->indirect_block, block_type);
        pthread_mutex_unlock(get_lock());
        #endif
    }
    return rv;
}
