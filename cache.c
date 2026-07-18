#include <stdlib.h>
#include <string.h>
#ifdef __APPLE__
#include <sys/sysctl.h>
#else
#include <sys/sysinfo.h>
#endif
#include <sys/param.h>
#ifdef __linux__
#include <bsd/stdlib.h>
#endif
#include "disk.h"
#include "types.h"
#include "cache.h"
#include "lock.h"

void*
get_block(DiskInterface* disk, cache *cache, uint64_t inum, uint64_t pnum)
{
#ifdef CACHE_DISABLED
	return disk_get_block(disk, pnum);
#else
	// Check if block is already in cache using primary cache index
	int rv = pci_lookup(cache->pci, pnum);
	if (rv==-1) {
		// Block not in cache - need to load it
		
		// If no free cache slots, evict LRU entry
		if (cache->free_list==NULL) {
			// Get least recently used non-pinned cache entry
            LRU_List *curr = cache->lru->prev;
            while (cache->cache[curr->index].pin_count) curr = curr->prev;
            pthread_mutex_lock(&cache->cache[curr->index].lock);
			int cache_index = lru_pop(cache, &curr);
			
			// If evicted entry is dirty, write it back to disk
			if (cache->cache[cache_index].dirty_bit)
			{
				block_type_t *block_type = (block_type_t*)cache->cache[cache_index].page_data;
				// Write dirty data back to disk
				pthread_mutex_lock(get_lock());
				disk_write_block(disk, cache->cache[cache_index].block_number, cache->cache[cache_index].page_data);
				pthread_mutex_unlock(get_lock());
				free(cache->cache[cache_index].page_data);
				// Remove from dirty list if it's a data block
				if (*block_type==BLOCK_TYPE_DATA) dl_remove_block(cache->dirty_list, cache->cache[cache_index].inode_number, cache->cache[cache_index].block_number);
				// Remove from global dirty list
				gdl_pop(cache, &cache->cache[cache_index].gdl_pos);
			}
			// Remove old mapping from primary cache index
			pci_delete(cache->pci, cache->cache[cache_index].block_number);
			// Add evicted slot back to free list
			cache->free_list = fl_push(cache->free_list, cache_index);
		}
		
		// Get a free cache slot
		int index = cache->free_list->index;
		cache->free_list = fl_pop(cache->free_list);
		
		// Initialize the new cache entry
		cache->cache[index].dirty_bit = false;
		cache->cache[index].pin_count = 0;
		cache->cache[index].block_number = pnum;
		cache->cache[index].inode_number = inum;
		cache->cache[index].page_data = malloc(BLOCK_SIZE);
		
		// Load block data from disk into cache
		//printf("Copying page %llu into the cache!\n", pnum);
		disk_read_block(disk, pnum, cache->cache[index].page_data);
		
		// Add to LRU list (most recently used)
		cache->cache[index].lru_pos = lru_push(cache, index);
		cache->lru = cache->cache[index].lru_pos;
		
		// Add mapping to primary cache index
		pci_insert(cache->pci, pnum, index);
		pthread_mutex_unlock(&cache->cache[index].lock);
		return cache->cache[index].page_data;
	} else {
        // Block found in cache - move it to front of LRU list (most recently used)
        int cache_index = lru_pop(cache, &cache->cache[rv].lru_pos);
        cache->cache[rv].lru_pos = lru_push(cache, cache_index);
        cache->lru = cache->cache[rv].lru_pos;

		return cache->cache[rv].page_data;
	}
#endif
}

void
write_block(DiskInterface* disk, cache *cache, void *buf, int64_t inum, uint64_t pnum)
{
	#ifndef CACHE_DISABLED
	// Look up block in cache using primary cache index
	int index = pci_lookup(cache->pci, pnum);
	if (index==-1)
	{
		// Block not in cache - load it first
		get_block(disk, cache, inum, pnum);
		index = pci_lookup(cache->pci, pnum);
	}
	pthread_mutex_lock(&cache->cache[index].lock);
	
	// Get block type to determine if we need dirty list tracking
	block_type_t *block_type = (block_type_t*)cache->cache[index].page_data;
	
    // Actually write the data to the cache
	memcpy(cache->cache[index].page_data, buf, BLOCK_SIZE);
	
	if (!cache->cache[index].dirty_bit) {
		cache->cache[index].dirty_bit = true;
		
		if (*block_type == BLOCK_TYPE_DATA) {
			dl_insert(cache->dirty_list, inum, pnum);
		}
		
		cache->gdl = gdl_push(cache, index);
		cache->cache[index].gdl_pos = cache->gdl;
	}
	
	// Add to per-inode dirty list if it's a data block
	if (*block_type==BLOCK_TYPE_DATA) dl_insert(cache->dirty_list, inum, pnum);
	
	// Add to global dirty list for sync operations
	cache->gdl = gdl_push(cache, index);
	cache->cache[index].gdl_pos = cache->gdl;
	pthread_mutex_unlock(&cache->cache[index].lock);
	#endif
}

void increase_pin_count(DiskInterface* disk, cache *cache, uint64_t inum, uint64_t pnum)
{
#ifndef CACHE_DISABLED
    int index = pci_lookup(cache->pci, pnum);
    if (index==-1)
    {
        // Block not in cache - load it first
        get_block(disk, cache, inum, pnum);
        index = pci_lookup(cache->pci, pnum);
    }
    
    cache->cache[index].pin_count++;
#endif
}

void decrease_pin_count(DiskInterface* disk, cache *cache, uint64_t inum, uint64_t pnum)
{
#ifndef CACHE_DISABLED
    int index = pci_lookup(cache->pci, pnum);
    if (index==-1)
    {
        // Block not in cache - load it first
        get_block(disk, cache, inum, pnum);
        index = pci_lookup(cache->pci, pnum);
    }
    
    if (cache->cache[index].pin_count > 0) cache->cache[index].pin_count--;
#endif
}

void cache_fsync(DiskInterface* disk, cache *cache, uint64_t inum)
{
	#ifndef CACHE_DISABLED
	// Look up all dirty blocks for this specific inode
	DL_HM_LL *hmlist = dl_lookup(cache->dirty_list, inum);
	DL_HM_LL *prev;
	if (hmlist)
	{
		// Iterate through all dirty blocks for this inode
		DL_LL *list;
		list = hmlist->list;
		while (list)
		{
			// Find the cache entry for this block using primary cache index
			int index = pci_lookup(cache->pci, list->block_number);
			
			// Write the dirty block back to disk
			pthread_mutex_lock(get_lock());
			disk_write_block(disk, cache->cache[index].block_number, cache->cache[index].page_data);
			pthread_mutex_unlock(get_lock());
			
			// Mark as clean since it's now synced with disk
			cache->cache[index].dirty_bit=false;
			
			// Remove from dirty list
			list = dl_pop(list);
			
			// Remove from global dirty list
			gdl_pop(cache, &cache->cache[index].gdl_pos);
			cache->cache[index].gdl_pos = NULL;
		}
		// Remove entire inode entry from dirty list
		dl_delete(cache->dirty_list, inum);
	}
	#endif
}

void cache_sync(DiskInterface* disk, cache *cache)
{
	#ifndef CACHE_DISABLED
	// Sync all dirty blocks to disk using global dirty list
	while (cache->gdl_size > 0 && cache->gdl != NULL)
	{
		// Get the tail node (oldest dirty block) for FIFO processing
		GDL *tail_node = cache->gdl->prev;
		
		// Get cache entry index from global dirty list
        int index = gdl_pop(cache, &tail_node);
		
		// Check for valid index before accessing cache array
		if (index < 0 || index >= cache->cache_size) {
			break; // Invalid index, exit loop to prevent crash
		}
		
		block_type_t *block_type = (block_type_t*)cache->cache[index].page_data;
		
		// Write dirty block back to disk
		pthread_mutex_lock(get_lock());
		disk_write_block(disk, cache->cache[index].block_number, cache->cache[index].page_data);
		pthread_mutex_unlock(get_lock());
		
		// Mark as clean and clear dirty list position
		cache->cache[index].dirty_bit=false;
		cache->cache[index].gdl_pos=NULL;
		
		// Remove from per-inode dirty list if it's a data block
		if (*block_type==BLOCK_TYPE_DATA) dl_remove_block(cache->dirty_list, cache->cache[index].inode_number, cache->cache[index].block_number);
	}
	#endif
}

cache* alloc_cache()
{
	#ifndef CACHE_DISABLED
	// Determine cache size based on available system memory
	int gb_ram = 0;
	int64_t physical_memory;
#ifdef __APPLE__
	int mib[2];
    size_t length;

    // Set up the MIB (Management Information Base) for hw.memsize
    mib[0] = CTL_HW;
    mib[1] = HW_MEMSIZE;
    length = sizeof(int64_t);

    // Call sysctl to get the memory size
    if (sysctl(mib, 2, &physical_memory, &length, NULL, 0) == 0) {
        gb_ram = physical_memory / (1024 * 1024 * 1024);
    }
#else
	struct sysinfo info;
	sysinfo(&info);
	gb_ram = info.totalram / (1024 * 1024 * 1024);
	physical_memory = info.totalram;
#endif
	uint64_t cache_size = 0;
	
	// Cache sizing policy based on available RAM
	if (gb_ram < 2) cache_size = (64 * 1024 * 1024) / 4096;  // 64MB cache for low memory systems
	else if (gb_ram > 2 && gb_ram <= 16) cache_size = physical_memory / (8 * 4096);  // 1/8 of RAM
	else cache_size = MIN( (2*1024*1024), (physical_memory / (8 * 4096)));  // Cap at 2GB
	
	// Allocate main cache structure
	cache *cache = malloc(sizeof(struct cache));
	cache->cache_size = cache_size;
	
	// Allocate array of cache entries
	cache->cache = malloc(cache_size * sizeof(struct cache_entry_t));
	
	// Initialize all cache entries - clear page data pointers
	for (int i=0; i<cache->cache_size; i++)
	{
		cache->cache[i].page_data=NULL;
		cache->cache[i].lru_pos=NULL;
	}
	// Initialize all entries as clean
	for (int i=0; i<cache_size; i++)
	{
		cache->cache[i].dirty_bit = false;
	}
	
	// Initialize list sizes
	cache->lru_size = 0;
	cache->gdl_size = 0;
	
	// Allocate and initialize primary cache index hashmap
	cache->pci = malloc(sizeof(struct PCI_HM));
	for (int i=0; i<HASHMAP_SIZE; i++)
	{
		cache->pci->HashMap[i] = NULL;
	}
	
	// Allocate and initialize dirty list hashmap
	cache->dirty_list = malloc(sizeof(struct DL_HM));
	for (int i=0; i<HASHMAP_SIZE; i++)
	{
		cache->dirty_list->HashMap[i] = NULL;
	}
	
	// Initialize free list with all cache slots
	cache->free_list=NULL;
	for (int i=0; i<cache_size; i++) {
		//printf("Pushing cache index %d to free list.\n", i);
		cache->free_list = fl_push(cache->free_list, i);
	}
	
	// Initialize LRU and global dirty lists as empty
	cache->lru=NULL;
	cache->gdl=NULL;
	return cache;
	#else
	return NULL;
	#endif
}

void free_cache(cache *cache)
{
	#ifndef CACHE_DISABLED
	// Clean up global dirty list
	for (int i=cache->gdl_size; i>0; i--)
	{
		gdl_pop(cache, &cache->gdl);
	}
	
	// Clean up LRU list
	for (int i=cache->lru_size; i>0; i--)
	{
		lru_pop(cache, &cache->lru);
	}
	
	// Clean up free list
	while (cache->free_list!=NULL)
	{
		//printf("Popping cache index %d from free list.\n", cache->free_list->index);
		cache->free_list = fl_pop(cache->free_list);
	}
	
	// Clean up dirty list hashmap - free all chains
	for (int i=0; i<HASHMAP_SIZE; i++)
	{
		DL_HM_LL *hmlist = cache->dirty_list->HashMap[i];
		DL_HM_LL *prev;
		while (hmlist)
		{
			prev = hmlist;
			// Free the linked list of blocks for this inode
			DL_LL *list = hmlist->list;
			while (list)
			{
				list = dl_pop(list);
				hmlist->list = list;
			}
			hmlist = hmlist->next;
			// Securely clear memory before freeing
			arc4random_buf(prev, sizeof(struct DL_HM_LL));
			free(prev);
		}
	}
	arc4random_buf(cache->dirty_list, sizeof(struct DL_HM));
	free(cache->dirty_list);
	
	// Clean up primary cache index hashmap - free all chains
	for (int i=0; i<HASHMAP_SIZE; i++)
	{
		PCI_LL *prev;
		while (cache->pci->HashMap[i])
		{
			prev = cache->pci->HashMap[i];
			cache->pci->HashMap[i] = cache->pci->HashMap[i]->next;
			// Securely clear memory before freeing
			arc4random_buf(prev, sizeof(struct PCI_LL));
			free(prev);
		}
	}
	arc4random_buf(cache->pci, sizeof(struct PCI_HM));
	free(cache->pci);
	
	// Free all cached page data
	for (int i=0; i<cache->cache_size; i++)
	{
		if (cache->cache[i].page_data)
		{
			// Securely clear cached data before freeing
			arc4random_buf(cache->cache[i].page_data, BLOCK_SIZE);
			free(cache->cache[i].page_data);
		}
	}
	
	// Free cache entries array and main cache structure
	arc4random_buf(cache->cache, cache->cache_size * sizeof(struct cache_entry_t));
	free(cache->cache);
	arc4random_buf(cache, sizeof(struct cache));
	free(cache);
	#endif
}
