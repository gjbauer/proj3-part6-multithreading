#include <stdlib.h>
#include "lru.h"
#include "cache.h"

// Add a cache entry index to the head of the LRU (Least Recently Used) list
// The LRU list tracks cache usage order for eviction decisions
// Returns pointer to the new LRU node
LRU_List *lru_push(cache *cache, int index)
{
	LRU_List *list = cache->lru;
	// Allocate new node
	LRU_List *node = (LRU_List*)malloc(sizeof(struct LRU_List));
	node->index = index;
	
	if (cache->lru_size>0 && list != NULL)
	{
		// Insert into existing circular doubly-linked list
		node->next = list;
		node->prev = list->prev;
		list->prev = node;
		node->prev->next = node;
	}
	else
	{
		// First node - create circular links to self
		node->next = node;
		node->prev = node;
	}
	
	cache->lru_size++;
	return node;
}

// Remove the least recently used entry from the LRU list
// Returns the cache entry index of the evicted item
// Securely wipes the removed node before freeing
int64_t lru_pop(cache *cache, LRU_List **list)
{
    int64_t index;
    LRU_List *node_to_remove;
    
    if (list == NULL || *list == NULL) {
        return -1;  // Invalid node
    }
    
    node_to_remove = *list;
    
    // Store the index before freeing
    index = node_to_remove->index;
    
    if (cache->lru_size == 1) {
        // Only one node in the list
        cache->lru = NULL;
        *list = NULL;  // Update the caller's pointer
    } else {
        // Save the next node before removal
        LRU_List *next_node = node_to_remove->next;
        
        // Remove this node from the circular list
        node_to_remove->prev->next = next_node;
        next_node->prev = node_to_remove->prev;
        
        // Update the caller's pointer to the next node
        *list = next_node;
        
        // If we're removing the head, update cache->lru
        if (node_to_remove == cache->lru) {
            cache->lru = next_node;
        }
    }
    
    // Securely overwrite node data before freeing
    arc4random_buf(node_to_remove, sizeof(struct LRU_List));
    free(node_to_remove);
    
    cache->lru_size--;
    
    return index;
}

