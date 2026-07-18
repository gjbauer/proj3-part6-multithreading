#include <stdlib.h>
#include "lru.h"
#include "cache.h"

// Add a cache entry index to the global dirty list (GDL)
// The GDL tracks all dirty blocks regardless of inode or block type
// Returns pointer to the new GDL node
GDL *gdl_push(cache *cache, int index)
{
	GDL *list = cache->gdl;
	// Allocate new node
	GDL *node = (GDL*)malloc(sizeof(struct GDL));
	node->index = index;
	
	if (cache->gdl_size>0 && list != NULL)
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
	
	cache->gdl_size++;
	return node;
}

// Remove a specific node from the global dirty list (GDL)
// Securely wipes the removed node before freeing
int64_t gdl_pop(cache *cache, GDL **list)
{
    int64_t index;
    GDL *node_to_remove;
    
    if (*list == NULL) {
        return -1;  // Invalid node
    }
    
    node_to_remove = *list;
    
    // Store the index before freeing
    index = node_to_remove->index;
    
    if (cache->gdl_size == 1) {
        // Only one node in the list
        cache->gdl = NULL;
        *list = NULL;
    } else {
        // Remove this node from the circular list
        node_to_remove->prev->next = node_to_remove->next;
        node_to_remove->next->prev = node_to_remove->prev;
        
        // If we're removing the head, update cache->gdl to the next node
        if (node_to_remove == cache->gdl) {
            cache->gdl = node_to_remove->next;
        }
        
        // Update the caller's pointer to NULL since we're removing this node
        *list = NULL;
    }
    
    // Securely overwrite node data before freeing
    arc4random_buf(node_to_remove, sizeof(struct GDL));
    free(node_to_remove);
	
	cache->gdl_size--;
    return index;
}

