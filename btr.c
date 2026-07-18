#include <stdio.h>
#include "journal.h"
#include <string.h>
#include <errno.h>
#include "btr.h"
#include "disk.h"
#include "hash.h"
#ifdef __linux__
#include <bsd/stdlib.h>
#else
#include <stdlib.h>
#endif
#include "lock.h"

int btree_write(DiskInterface *disk, cache *cache, uint64_t block_num)
{
	int rv = 0;
	block_type_t *block_type = (block_type_t*) get_block(disk, cache, 0, block_num);
	if (*block_type != BLOCK_TYPE_BTREE_NODE)
	{
		fprintf(stderr, "ERROR: Not a valid B-Tree node!\n");
		return -1;
	}
	BTreeNode *node = (BTreeNode*)( block_type + 1 );

	pthread_mutex_lock(get_lock());
	if (!disk_write_block(disk, node->block_number, block_type))
	{
		rv++;
		decrease_pin_count(disk, cache, 0, node->block_number);
	}
	pthread_mutex_unlock(get_lock());

	for (int i=0; i <= node->num_keys; i++)
	{
		if (node->children[i])
		{
			rv += btree_write(disk, cache, node->children[i]);
		}
		else break;
	}
	return rv;
}

/**
 * Create a new B-tree node on disk
 * Allocates a disk block and initializes the node structure
 */
BTreeNode* btree_node_create(DiskInterface* disk, cache *cache, bool is_leaf, uint64_t* page)
{
	// Allocate a new disk block for this node
	*page = alloc_page(disk, cache);
    if (!*page) return NULL;
	
	// Get pointer to the allocated block
	block_type_t *block_type = (block_type_t*)get_block(disk, cache, 0, *page);
	*block_type = BLOCK_TYPE_BTREE_NODE;
    
	BTreeNode *node = (BTreeNode*)( (block_type_t*) block_type + 1 );
	
	// Initialize node metadata
	node->block_number = *page;
	node->is_leaf = is_leaf;
	node->key = 0;
	node->num_keys = 0;
	node->value = 0;
	node->parent = 0;
	node->left_sibling = 0;
	node->right_sibling = 0;
	
	// Initialize all keys and children to 0
	for(int i=0; i<MAX_KEYS; i++) node->keys[i]=0;
	for(int i=0; i<=MAX_KEYS; i++) node->children[i]=0;
    
    write_block(disk, cache, block_type, 0, node->block_number);
	
	return node;
}

/**
 * Free a B-tree node and return its disk block to the free pool
 */
void btree_node_free(DiskInterface* disk, cache *cache, uint64_t block)
{
	free_page(disk, cache, block);
}

/**
 * Read a B-tree node from disk into memory
 * Copies node data from disk block to provided structure
 */
int btree_node_read(DiskInterface* disk, cache *cache, uint64_t block_num, BTreeNode* node)
{
	//printf("Reading B-Tree block %llu\n", block_num);
	block_type_t *block_type = (block_type_t*) get_block(disk, cache, 0, block_num);
	if (*block_type != BLOCK_TYPE_BTREE_NODE)
	{
		fprintf(stderr, "ERROR: Not a valid B-Tree node!\n");
		return -1;
	}
	BTreeNode *disk_node = (BTreeNode*)( block_type + 1 );
	
	// Copy node data from disk to memory structure
	void *ptr = memcpy((char*)node, (char*)disk_node, sizeof(struct BTreeNode));

	if (!ptr)
	{
		fprintf(stderr, "ERROR: Could not copy B-Tree node contents!\n");
		return -1;
	}
	
	return 0;
}

/**
 * Write a B-tree node from memory to disk
 * Copies node data from memory structure to disk block
 */
int btree_node_write(DiskInterface* disk, cache *cache, BTreeNode* node)
{
	block_type_t *block_type = (block_type_t*) get_block(disk, cache, 0, node->block_number);
	if (*block_type != BLOCK_TYPE_BTREE_NODE)
	{
		fprintf(stderr, "ERROR: Not a valid B-Tree node!\n");
		return -1;
	}
	BTreeNode *mem_node = (BTreeNode*)( (block_type_t*)block_type + 1 );
	
	// Copy node data from memory to disk
	void *ptr = memcpy((char*)mem_node, (char*)node, sizeof(struct BTreeNode));
	
	if (!ptr)
	{
		fprintf(stderr, "ERROR: Could not copy B-Tree node contents!\n");
		return -1;
	}
    
	write_block(disk, cache, block_type, 0, node->block_number);
	
	return 0;
}

/**
 * Search for a key in the B-tree
 * Recursively traverses the tree to find the specified key
 */
uint64_t btree_search(DiskInterface* disk, cache *cache, uint64_t node_block, uint64_t key)
{
    BTreeNode node;
    if (btree_node_read(disk, cache, node_block, &node) != 0) {
        return 0;
    }
    
    if (node.is_leaf) {
        if (node.key == key) {
            return node.block_number;
        }
        return 0;
    }
    
    // Find the correct child to search
    int child_idx = 0;
    for (int i = 0; i < node.num_keys; i++) {
        if (key <= node.keys[i]) {
            child_idx = i;
            break;
        }
        child_idx = i + 1;
    }
    
    // Search the correct child
    if (node.children[child_idx] != 0) {
        return btree_search(disk, cache, node.children[child_idx], key);
    }
    
    return 0;
}

/**
 * Find the depth of a node in the B-tree
 * Follows the leftmost path down to a leaf to determine depth
 */
int btree_find_depth(DiskInterface* disk, cache *cache, uint64_t node_block)
{
	BTreeNode node;
	btree_node_read(disk, cache, node_block, &node);
	
	int depth=0;
	while (true) {
		if (node.is_leaf) {
			arc4random_buf(&node, sizeof(struct BTreeNode));
            goto exit;  // Reached a leaf, return current depth
		}
	
		// Follow the leftmost child
		if (node.children[0] != 0) {
			btree_node_read(disk, cache, node.children[0], &node);
			depth++;
		} else {
            goto exit;
		}
	}
exit:
    arc4random_buf(&node, sizeof(struct BTreeNode));
    return depth;
}

/**
 * Find the height of the B-tree from a given node
 * Height is the number of levels from this node to the deepest leaf
 */
int btree_find_height(DiskInterface* disk, cache *cache, uint64_t node_block)
{
	BTreeNode node;
	btree_node_read(disk, cache, node_block, &node);
	
	// Special case: single node tree
	if (node.parent==0 && node.children[0]==0)
	{
		arc4random_buf(&node, sizeof(struct BTreeNode));
		return 0;
	}
	
	int height=0;
	// Follow leftmost path to count levels
	while (!node.is_leaf)
	{
		btree_node_read(disk, cache, node.children[0], &node);
		height++;
	}
	arc4random_buf(&node, sizeof(struct BTreeNode));
	return height;
}

/**
 * Find the minimum key in a B-tree subtree
 * Recursively follows the leftmost path to find the smallest key
 */
int btree_find_minimum(DiskInterface* disk, cache *cache, uint64_t root_block)
{
	BTreeNode root;
	btree_node_read(disk, cache, root_block, &root);
	
	// Follow leftmost child until we reach a leaf
	BTreeNode first_child;
	btree_node_read(disk, cache, root.children[0], &first_child);
	if (first_child.is_leaf)
	{
		int fc_key = first_child.key;
		arc4random_buf(&root, sizeof(struct BTreeNode));
		arc4random_buf(&first_child, sizeof(struct BTreeNode));
		return fc_key;
	}
	else
	{
		int min = btree_find_minimum(disk, cache, first_child.block_number);
		arc4random_buf(&root, sizeof(struct BTreeNode));
		arc4random_buf(&first_child, sizeof(struct BTreeNode));
		return min;
	}
}

/**
 * Find the maximum key in a B-tree subtree
 * Recursively follows the rightmost path to find the largest key
 */
uint64_t btree_find_maximum(DiskInterface* disk, cache *cache, uint64_t root_block)
{
	BTreeNode root;
	btree_node_read(disk, cache, root_block, &root);
	
	uint64_t key;
	// Base case: if this is a leaf, return its key
	if (root.is_leaf)
	{
		key = root.key;
		arc4random_buf(&root, sizeof(struct BTreeNode));
		return key;
	}
	
	// Find the rightmost non-null child and recurse
	for (int i = root.num_keys; i >= 0; i--) {
		if (root.children[i] != 0) {
			key = btree_find_maximum(disk, cache, root.children[i]);
			arc4random_buf(&root, sizeof(struct BTreeNode));
			return key;
		}
	}
	
	arc4random_buf(&root, sizeof(struct BTreeNode));
	return 0;  // Should not reach here in a valid tree
}

/**
 * Insert a node into a non-full internal node
 * Finds the correct position and shifts existing children as needed
 */
int btree_insert_nonfull(DiskInterface* disk, cache *cache, BTreeNode *root, BTreeNode *node)
{
	if (root->is_leaf) {
		fprintf(stderr, "ERROR: Trying to insert into leaf node\n");
		return -1;
	} else {
		// Find the correct position for the new child
		int child_pos = 0;
		for(int j = 0; j <= root->num_keys; j++) {
			if(root->children[j] != 0) {
				BTreeNode child;
				btree_node_read(disk, cache, root->children[j], &child);
				if(child.is_leaf && child.key < node->key) {
					child_pos = j + 1;
				} else if(!child.is_leaf) {
					uint64_t max_key = btree_find_maximum(disk, cache, child.block_number);
					if(max_key < node->key) {
						child_pos = j + 1;
					}
                    else {
                        child_pos = j;
                    }
				}
				arc4random_buf(&child, sizeof(struct BTreeNode));
			}
            else break;
		}
        
        if (child_pos > root->num_keys) {
            child_pos = root->num_keys;
        }
		
		// Shift existing children to make room
		for(int j = root->num_keys; j >= child_pos; j--) {
			root->children[j + 1] = root->children[j];
		}
        for(int j = root->num_keys; j > child_pos; j--) {
            root->keys[j] = root->keys[j - 1];  // Shift keys correctly
        }
		
		// Insert the new child
        root->keys[child_pos] = btree_find_maximum(disk, cache, node->block_number);
		root->children[child_pos] = node->block_number;
		node->parent = root->block_number;
		root->num_keys++;
        btree_node_write(disk, cache, node);
		
		//printf("Placing node with key %llu at child position %d\n", node->key, child_pos);
		//printf("Block number = %llu\n", node->block_number);
	}
	
	return 0;
}

int btree_insertion_search(DiskInterface* disk, cache *cache, uint64_t root_block, uint64_t key)
{
	BTreeNode root;
	btree_node_read(disk, cache, root_block, &root);
	
	if (root.children[0] == 0) {
		return root.block_number;
	}
	
	if (btree_find_depth(disk, cache, root_block)-1 <= 0) {
		return root.block_number;
	}
	
	BTreeNode node;
	memcpy(&node, &root, sizeof(struct BTreeNode));
	int current_depth = 0;
	while (current_depth < btree_find_depth(disk, cache, root_block)-1) {
		int child_index = 0;
		
		for (int i = 0; i < node.num_keys; i++) {
			if (node.children[i] != 0) {
				uint64_t child_max = btree_find_maximum(disk, cache, node.children[i]);
				if (key <= child_max) {
					child_index = i;
					break;
				}
				child_index = i + 1;
			}
		}
		
		if (child_index > node.num_keys) {
			child_index = node.num_keys;
		}
		
		bool descended = false;
		if (node.children[child_index] != 0) {
			btree_node_read(disk, cache, node.children[child_index], &node);
			current_depth++;
			descended = true;
		} else {
			for (int i = node.num_keys; i >= 0; i--) {
				if (node.children[i] != 0) {
					btree_node_read(disk, cache, node.children[i], &node);
					current_depth++;
					descended = true;
					break;
				}
			}
		}
		
		if (!descended) {
			break;
		}
	}
	int rv = node.block_number;

	arc4random_buf(&node, sizeof(struct BTreeNode));
	return rv;
}

/**
 * Update parent node keys after a child modification
 * Ensures parent keys reflect the maximum values of their child subtrees
 */
void btree_update_parent_keys(DiskInterface* disk, cache *cache, BTreeNode* node)
{
	if (node->parent == 0) return;  // No parent to update
	
	BTreeNode parent;
	btree_node_read(disk, cache, node->parent, &parent);
	
	// Update all keys in the parent based on the maximum values of each child
	for (int i = 0; i < parent.num_keys; i++) {
		if (parent.children[i] != 0) {
			parent.keys[i] = btree_find_maximum(disk, cache, parent.children[i]);
		}
	}
	
	btree_node_write(disk, cache, &parent);
	arc4random_buf(&parent, sizeof(struct BTreeNode));
}

int btree_insert(DiskInterface* disk, cache *cache, uint64_t root_block, uint64_t key, uint64_t value, FileType type)
{
	uint64_t page;
	BTreeNode *node = btree_node_create(disk, cache, true, &page);
	node->key = key;
	node->value = value;
	node->type = type;
	btree_node_write(disk, cache, node);
	
	int target_block = btree_insertion_search(disk, cache, root_block, key);
	//printf("Target block: %d\n", target_block);
	BTreeNode target;
	btree_node_read(disk, cache, target_block, &target);
	
	if (target.num_keys == MAX_KEYS) {
		if (target.keys[MAX_KEYS - 1] < key && target.children[MAX_KEYS]==0)
		{
			target.children[MAX_KEYS]=node->block_number;
			node->parent=target.block_number;
			btree_node_write(disk, cache, &target);
		} else {
			if (target.parent != 0) {
				BTreeNode parent;
				btree_node_read(disk, cache, target.parent, &parent);
				int i;
				for(i=0; i<MAX_KEYS && parent.keys[i] < btree_find_maximum(disk, cache, target.block_number) && parent.keys[i]!=0; i++);
				btree_split_child(disk, cache, &parent, i, &target);
				target_block = btree_insertion_search(disk, cache, root_block, key);
				btree_node_read(disk, cache, target_block, &target);
			} else {
				btree_split_root(disk, cache, &target);
				target_block = btree_insertion_search(disk, cache, root_block, key);
				btree_node_read(disk, cache, target_block, &target);
			}
			btree_insert_nonfull(disk, cache, &target, node);
			btree_node_write(disk, cache, &target);
		}
	}
	else
	{
		btree_insert_nonfull(disk, cache, &target, node);
		btree_node_write(disk, cache, &target);
	}

	btree_update_parent_keys(disk, cache, &target);
	btree_node_write(disk, cache, node);
	arc4random_buf(&target, sizeof(struct BTreeNode));
	
	return 0;
}

int btree_insert_nocreate(DiskInterface* disk, cache *cache, uint64_t root_block, uint64_t key, uint64_t value, FileType type, BTreeNode *node)
{
	uint64_t page;
	
	int target_block = btree_insertion_search(disk, cache, root_block, key);
	//printf("Target block: %d\n", target_block);
	BTreeNode target;
	btree_node_read(disk, cache, target_block, &target);
	
	if (target.num_keys == MAX_KEYS) {
		if (target.keys[MAX_KEYS - 1] < key && target.children[MAX_KEYS]==0)
		{
			target.children[MAX_KEYS]=node->block_number;
			node->parent=target.block_number;
			btree_node_write(disk, cache, &target);
		} else {
			if (target.parent != 0) {
				BTreeNode parent;
				btree_node_read(disk, cache, target.parent, &parent);
				int i;
				for(i=0; i<MAX_KEYS && parent.keys[i] < btree_find_maximum(disk, cache, target.block_number) && parent.keys[i]!=0; i++);
				btree_split_child(disk, cache, &parent, i, &target);
				target_block = btree_insertion_search(disk, cache, root_block, key);
				btree_node_read(disk, cache, target_block, &target);
			} else {
				btree_split_root(disk, cache, &target);
				target_block = btree_insertion_search(disk, cache, root_block, key);
				btree_node_read(disk, cache, target_block, &target);
			}
			btree_insert_nonfull(disk, cache, &target, node);
			btree_node_write(disk, cache, &target);
		}
	}
	else
	{
		btree_insert_nonfull(disk, cache, &target, node);
		btree_node_write(disk, cache, &target);
	}

	btree_update_parent_keys(disk, cache, &target);
	btree_node_write(disk, cache, node);
	arc4random_buf(&target, sizeof(struct BTreeNode));
	
	return 0;
}

/**
 * Borrow a child from the left sibling to rebalance the tree
 * Used during deletion when a node becomes too small
 */
uint64_t btree_borrow_left(DiskInterface* disk, cache *cache, BTreeNode *node)
{
	uint64_t rv = 0;
	if (node->left_sibling==0) return rv;  // No left sibling
	else {
		BTreeNode left_sibling;
		btree_node_read(disk, cache, node->left_sibling, &left_sibling);
		
		// Can't borrow if sibling has minimum keys
		if (left_sibling.num_keys<=MIN_KEYS) return rv;
		else {
			// Find the rightmost child to borrow
			for (int i = left_sibling.num_keys; i >= 0; i--) {
				if (left_sibling.children[i] != 0) {
					if (i < left_sibling.num_keys) left_sibling.keys[i]=0;
					rv = left_sibling.children[i];  // Return borrowed child
					left_sibling.children[i] = 0;
					left_sibling.num_keys--;
					break;
				}
			}
		}
		btree_node_write(disk, cache, &left_sibling);
		arc4random_buf(&left_sibling, sizeof(struct BTreeNode));
	}
	return rv;
}

/**
 * Borrow a child from the right sibling to rebalance the tree
 * Used during deletion when a node becomes too small
 */
uint64_t btree_borrow_right(DiskInterface* disk, cache *cache, BTreeNode *node)
{
	uint64_t rv = 0;
	if (node->right_sibling==0) return rv;  // No right sibling
	else {
		BTreeNode right_sibling;
		btree_node_read(disk, cache, node->right_sibling, &right_sibling);
		
		// Can't borrow if sibling has minimum keys
		if (right_sibling.num_keys<=MIN_KEYS) return rv;
		else {
			// Borrow the leftmost child
			rv = right_sibling.children[0];
			
			// Shift all keys and children left
			for(int i = 0; i < MAX_KEYS; i++) {
				right_sibling.keys[i] = right_sibling.keys[i+1];
			}
			for(int i = 0; i <= MAX_KEYS; i++) {
				right_sibling.children[i] = right_sibling.children[i+1];
			}
			right_sibling.keys[MAX_KEYS-1] = 0;
			right_sibling.children[MAX_KEYS] = 0;
			right_sibling.num_keys--;
		}
		btree_node_write(disk, cache, &right_sibling);
		arc4random_buf(&right_sibling, sizeof(struct BTreeNode));
	}
	return rv;
}

void btree_remove_key(DiskInterface* disk, cache *cache, uint64_t root_block, uint64_t key)
{
    BTreeNode current;
    btree_node_read(disk, cache, root_block, &current);
    
    while (!current.is_leaf) {
        BTreeNode first_child;
        btree_node_read(disk, cache, current.children[0], &first_child);
        
        if (first_child.is_leaf) {
            break;
        }
        
        int child_idx = current.num_keys;
        for (int i = current.num_keys; i > 0; i--) {
            if (key <= current.keys[i]) {
                child_idx = i;
                break;
            }
            child_idx = i - 1;
        }
        
        btree_node_read(disk, cache, current.children[child_idx], &current);
    }
    btree_node_read(disk, cache, current.parent, &current);
    
    int child_to_remove = -1;
    for (int i = 0; i <= current.num_keys; i++) {
        if (current.children[i] != 0) {
            BTreeNode child;
            btree_node_read(disk, cache, current.children[i], &child);
            if (child.is_leaf && child.key == key) {
                child_to_remove = i;
                arc4random_buf(&child, sizeof(BTreeNode));
                break;
            }
            arc4random_buf(&child, sizeof(BTreeNode));
        }
    }
    
    if (child_to_remove == -1) {
        printf("Key %llu not found in children!\n", key);
        return;
    }
    
    // Remove the child
    for (int j = child_to_remove; j < current.num_keys; j++) {
        current.keys[j] = current.keys[j+1];
    }
    for (int j = child_to_remove; j <= current.num_keys; j++) {
        current.children[j] = current.children[j+1];
    }
    
    current.num_keys--;
    
    if (child_to_remove == current.num_keys) {
        current.keys[current.num_keys] = 0;
    }
    current.children[current.num_keys + 1] = 0;
    
    btree_node_write(disk, cache, &current);
    
    // Now handle underflow...
    uint64_t rv = 0;
    if (current.num_keys < MIN_KEYS) {
        rv = btree_borrow_left(disk, cache, &current);
        if (rv==0) {
            rv = btree_borrow_right(disk, cache, &current);
            if (rv==0) {
                if (current.parent != 0)
                {
                    BTreeNode grandparent;
                    btree_node_read(disk, cache, current.parent, &grandparent);
                    int j;
                    for(j=0; j<MAX_KEYS && grandparent.keys[j] < key && grandparent.keys[j]!=0; j++);
                    btree_merge_children(disk, cache, &grandparent, j);
                    btree_node_write(disk, cache, &grandparent);
                    arc4random_buf(&grandparent, sizeof(struct BTreeNode));
                }
                else if (btree_find_depth(disk, cache, current.block_number) > 1)
                {
                    btree_merge_children(disk, cache, &current, 0);
                    btree_node_write(disk, cache, &current);
                    btree_promote_root(disk, cache, &current);
                }
            }
        }
    }
}

uint64_t btree_delete(DiskInterface* disk, cache *cache, uint64_t root_block, uint64_t key)
{
	uint64_t rv = btree_search(disk, cache, root_block, key);
	BTreeNode node;
	
	if (rv!=0)
	{
        	btree_node_read(disk, cache, rv, &node);
		btree_remove_key(disk, cache, node.parent, key);
		btree_node_free(disk, cache, rv);
	}
	
	return rv;
}

void btree_split_root(DiskInterface* disk, cache *cache, BTreeNode* root)
{
    uint64_t page;
    BTreeNode *child_a = btree_node_create(disk, cache, false, &page);
	BTreeNode *child_b = btree_node_create(disk, cache, false, &page);
	child_a->right_sibling = child_b->block_number;
	child_b->left_sibling = child_a->block_number;
	
	for (int i = 0; i < MIN_KEYS; i++) {
		child_a->children[i] = root->children[i];
		if (root->children[i] != 0) {
			BTreeNode child;
			btree_node_read(disk, cache, root->children[i], &child);
			child.parent = child_a->block_number;
			btree_node_write(disk, cache, &child);
			arc4random_buf(&child, sizeof(struct BTreeNode));
		}
		child_a->num_keys++;
	}
	child_a->children[MIN_KEYS] = root->children[MIN_KEYS];
	if (root->children[MIN_KEYS] != 0) {
		BTreeNode child;
		btree_node_read(disk, cache, root->children[MIN_KEYS], &child);
		child.parent = child_a->block_number;
		btree_node_write(disk, cache, &child);
		arc4random_buf(&child, sizeof(struct BTreeNode));
	}
	
	for (int i = MIN_KEYS + 1; i < root->num_keys; i++) {
		child_b->keys[i - MIN_KEYS - 1] = root->keys[i];
		child_b->num_keys++;
	}
	for (int i = MIN_KEYS + 1; i <= root->num_keys; i++) {
		child_b->children[i - MIN_KEYS - 1] = root->children[i];
		if (root->children[i] != 0) {
			BTreeNode child;
			btree_node_read(disk, cache, root->children[i], &child);
			child.parent = child_b->block_number;
			btree_node_write(disk, cache, &child);
			arc4random_buf(&child, sizeof(struct BTreeNode));
		}
	}
    
    for (int i = 0; i < child_a->num_keys; i++) {
        if (child_a->children[i] != 0) {
            child_a->keys[i] = btree_find_maximum(disk, cache, child_a->children[i]);
        }
    }
    
    for (int i = 0; i < child_b->num_keys; i++) {
        if (child_b->children[i] != 0) {
            child_b->keys[i] = btree_find_maximum(disk, cache, child_b->children[i]);
        }
    }
	
	if (root->children[MAX_KEYS] != 0) {
		child_b->children[child_b->num_keys] = root->children[MAX_KEYS];
		BTreeNode child;
		btree_node_read(disk, cache, root->children[MAX_KEYS], &child);
		child.parent = child_b->block_number;
		btree_node_write(disk, cache, &child);
		arc4random_buf(&child, sizeof(struct BTreeNode));
	}
	
	root->is_leaf = false;
	root->num_keys = 2;
	root->children[0] = child_a->block_number;
	root->children[1] = child_b->block_number;
	
	// Clear remaining slots
	for (int i = 1; i < MAX_KEYS; i++) {
		root->keys[i] = 0;
	}
	for (int i = 2; i <= MAX_KEYS; i++) {
		root->children[i] = 0;
	}
	
	child_a->parent = root->block_number;
	child_b->parent = root->block_number;
	root->keys[0] = btree_find_maximum(disk, cache, child_a->block_number);
	root->keys[1] = btree_find_maximum(disk, cache, child_b->block_number);
	btree_node_write(disk, cache, child_a);
	btree_node_write(disk, cache, child_b);
	btree_node_write(disk, cache, root);
}

void btree_promote_root(DiskInterface* disk, cache *cache, BTreeNode* root)
{
	int page = root->block_number;
	
	BTreeNode child;
	btree_node_read(disk, cache, root->children[0], &child);
	
	memcpy(root, &child, sizeof(struct BTreeNode));
	
	root->block_number = page;
	root->parent = 0;
	
	btree_node_write(disk, cache, root);
	btree_node_free(disk, cache, child.block_number);
	
	for (int i = 0; i <= root->num_keys; i++) {
		if (root->children[i] != 0) {
			btree_node_read(disk, cache, root->children[i], &child);
			child.parent = root->block_number;
			btree_node_write(disk, cache, &child);
			arc4random_buf(&child, sizeof(struct BTreeNode));
		}
	}
}

void btree_split_child(DiskInterface* disk, cache *cache, BTreeNode* node, int index, BTreeNode* child)
{
	uint64_t page;
	BTreeNode *child_b = btree_node_create(disk, cache, false, &page);
	child_b->parent = node->block_number;
	child->right_sibling = child_b->block_number;
	child_b->left_sibling = child->block_number;
	
	for (int i = MIN_KEYS + 1; i < child->num_keys; i++) {
		child_b->keys[i - MIN_KEYS - 1] = child->keys[i];
		child->keys[i] = 0;
		child_b->num_keys++;
	}
	
	for (int i = MIN_KEYS + 1; i <= child->num_keys; i++) {
		child_b->children[i - MIN_KEYS - 1] = child->children[i];
		if (child_b->children[i - MIN_KEYS - 1] != 0) {
			BTreeNode grandchild;
			btree_node_read(disk, cache, child_b->children[i - MIN_KEYS - 1], &grandchild);
			grandchild.parent = child_b->block_number;
			btree_node_write(disk, cache, &grandchild);
			arc4random_buf(&grandchild, sizeof(struct BTreeNode));
		}
		child->children[i] = 0;
	}
	
	child->keys[MIN_KEYS] = 0;
	child->num_keys = MIN_KEYS;
	
	if (node->num_keys < MAX_KEYS) {
		for (int i = node->num_keys; i > index; i--) {
			node->children[i + 1] = node->children[i];
		}
		
		node->children[index + 1] = child_b->block_number;
		node->keys[index] = btree_find_maximum(disk, cache, child_b->block_number);
		child_b->parent = node->block_number;
		node->num_keys++;
		for (int i = 0; i < node->num_keys; i++) {
			if (node->children[i] != 0) {
				node->keys[i] = btree_find_maximum(disk, cache, node->children[i]);
			}
		}
	} else {
		if (node->parent != 0) {
			BTreeNode grandparent;
			btree_node_read(disk, cache, node->parent, &grandparent);
			int parent_index;
			for (parent_index = 0; parent_index <= grandparent.num_keys; parent_index++) {
				if (grandparent.children[parent_index] == node->block_number) break;
			}
			btree_split_child(disk, cache, &grandparent, parent_index, node);
			btree_node_write(disk, cache, &grandparent);
			arc4random_buf(&grandparent, sizeof(struct BTreeNode));
		} else {
			btree_split_root(disk, cache, node);
		}
		
		BTreeNode current_parent;
		btree_node_read(disk, cache, child->parent, &current_parent);
		int new_index;
		for (new_index = 0; new_index <= current_parent.num_keys; new_index++) {
			if (current_parent.children[new_index] == child->block_number) break;
		}
		
		if (current_parent.num_keys < MAX_KEYS) {
			for (int i = current_parent.num_keys; i > new_index; i--) {
				current_parent.children[i + 1] = current_parent.children[i];
			}
			
			current_parent.children[new_index + 1] = child_b->block_number;
			
			current_parent.num_keys++;
			for (int i = 0; i < current_parent.num_keys; i++) {
				if (current_parent.children[i] != 0) {
					current_parent.keys[i] = btree_find_maximum(disk, cache, current_parent.children[i]);
				}
			}
		}

		btree_node_write(disk, cache, child);
		btree_node_write(disk, cache, child_b);
		btree_node_write(disk, cache, &current_parent);
		arc4random_buf(&current_parent, sizeof(struct BTreeNode));
	}
}

void btree_merge_children(DiskInterface* disk, cache *cache, BTreeNode* parent, int index)
{
	if (index == MAX_KEYS) return btree_merge_children(disk, cache, parent, index-1);
	else if (parent->children[index] == 0 || parent->children[index+1]==0) return;
	BTreeNode child_a;
	btree_node_read(disk, cache, parent->children[index], &child_a);
	BTreeNode child_b;
	btree_node_read(disk, cache, parent->children[index+1], &child_b);
	child_a.right_sibling = child_b.right_sibling;
	
	for (int i = MIN_KEYS + 1; i < MAX_KEYS; i++) {
		child_a.keys[i] = child_b.keys[i - MIN_KEYS - 1];
		child_a.num_keys++;
	}
	for (int i = MIN_KEYS + 1; i <= MAX_KEYS; i++) {
		child_a.keys[i] = child_b.children[i - MIN_KEYS - 1];
	}
	
	btree_node_write(disk, cache, &child_a);
	arc4random_buf(&child_a, sizeof(struct BTreeNode));
	
	for(int i=index+1; i<MAX_KEYS-1; i++)
	{
		parent->keys[i] = parent->keys[i+1];
	}
	parent->keys[MAX_KEYS - 1] = 0;
	
	for(int i=index+1; i<MAX_KEYS; i++)
	{
		parent->children[i] = parent->children[i+1];
	}
	
	btree_node_free(disk, cache, child_b.block_number);
	
	parent->num_keys--;
	
	if (parent->num_keys < MIN_KEYS) {
		if (parent->parent == 0) {
			if (parent->children[1]==0) {
				printf("Promoting root!\n");
				btree_promote_root(disk, cache, parent);
			}
		} else {
			int rv = btree_borrow_left(disk, cache, parent);
			if (rv==-1) {
				rv = btree_borrow_right(disk, cache, parent);
				if (rv==-1) {
					BTreeNode grandparent;
					btree_node_read(disk, cache, parent->parent, &grandparent);
					int j;
					for(j=0; j<MAX_KEYS && grandparent.keys[j] < btree_find_maximum(disk, cache, parent->block_number) && grandparent.keys[j]!=0; j++);
					btree_merge_children(disk, cache, &grandparent, j);
					btree_node_write(disk, cache, &grandparent);
					arc4random_buf(&grandparent, sizeof(struct BTreeNode));
				}
			}
		}
	}
}

/**
 * Print B-tree structure for debugging
 * Recursively traverses and displays the tree with indentation showing levels
 */
void btree_print(DiskInterface* disk, cache *cache, uint64_t root_block, int level)
{
	BTreeNode node;
	btree_node_read(disk, cache, root_block, &node);
	printf("%*sBlock %llu: ", level*2, "", root_block);  // Indent based on level
	
	if (node.is_leaf) {
		// Print leaf node information
		printf("LEAF key=%llu parent=%llu value=%llu\n", node.key, node.parent, node.value);
	} else {
		// Print internal node information
		printf("INTERNAL keys=[");
		for(int i = 0; i < node.num_keys; i++) {
			printf("%llu", node.keys[i]);
			if (i < node.num_keys-1) printf(",");
		}
		printf("] children=[");
		for(int i = 0; i <= node.num_keys; i++) {
			printf("%llu", node.children[i]);
			if (i < node.num_keys) printf(",");
		}
		printf("]\n");
		
		// Recursively print all children with increased indentation
		for(int i = 0; i <= node.num_keys; i++) {
			if (node.children[i] != 0) {
				btree_print(disk, cache, node.children[i], level+1);
			}
		}
	}
}

/**
 * Main function - Interactive B-tree testing program
 * Provides a command-line interface for testing B-tree operations
 */
/*int main()
{
	// Open disk image and initialize B-tree
	DiskInterface* disk = disk_open("my.img");
	alloc_page(disk);  // Reserve block 0
    uint64_t page;
	BTreeNode *root = btree_node_create(disk, cache, false, &page);  // Create root as internal node
	
	// Interactive loop for testing B-tree operations
	while (true) {
		printf("Select 1 to insert a key, and 2 to search for a key, and 3 for debug print, and 4 to delete a key: ");
		int choice, key;
		scanf("%d", &choice);
		switch (choice) {
			case 1:
				printf("Key to insert: ");
				scanf("%d", &key);
				btree_insert(disk, cache, root->block_number, key);
				break;
			case 2:
				printf("Key to search: ");
				scanf("%d", &key);
				btree_search(disk, cache, root->block_number, key);
				break;
			case 3:
				btree_print(disk, cache, root->block_number, 1);
				break;
			case 4:
				printf("Key to delete: ");
				scanf("%d", &key);
				btree_delete(disk, cache, root->block_number, key);
				break;
			default:
				return 0;  // Exit program
		}
	}
}*/
