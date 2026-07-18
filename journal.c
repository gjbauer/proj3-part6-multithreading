#include "config.h"
#include "superblock.h"
#include "journal.h"
#include "metadata-api.h"
#include "inode.h"
#include "lock.h"

void initialize_journal_entry(DiskInterface *disk, cache *cache, journal_entry_t *entry)
{
    //printf("initialize_journal_entry called with type: 0x%x\n", entry->type);
    printf("initialize_journal_entry called with type: ");
    switch (entry->type)
    {
        case MKNOD:
            printf("MKNOD\n");
            entry->mknod.btree_block = 0;
            if ( FILE_TYPE_DIRECTORY == ( entry->mknod.mode & S_IFMT) )
            {
                // For directories, find the inode number from the path
                InodeBtreePair *pair = item_search(disk, cache, entry->mknod.path);
                entry->mknod.inode_number = pair->inode_number;
                entry->mknod.btree_block = pair->btree_block;
                arc4random_buf(pair, sizeof(struct InodeBtreePair));
                free(pair);
                _truncate(disk, cache, entry->mknod.path, 0, true);
            }
            break;
        case UNLINK:
            printf("UNLINK\n");
            break;
        case LINK:
            printf("LINK\n");
            break;
        case CHMOD:
            printf("CHMOD\n");
            break;
        case TRUNCATE:
            printf("TRUNCATE\n");
            break;
        case WRITE:
            printf("WRITE\n");
            break;
        case RENAME:
            printf("RENAME\n");
            break;
        default:
            printf("INVALID TYPE\n");
            break;
    }

    Superblock sb;
    journal_entry_t *prev_entry;
    block_type_t *block_type;

    superblock_read(disk, cache, &sb);

    uint64_t journal_block = sb.journal_start + sb.journal_head;
    printf("Reading journal block %llu\n", journal_block);
    
    entry->block_number = journal_block;

    block_type = (block_type_t*)get_block(disk, cache, 0, journal_block);

    if (*block_type != BLOCK_TYPE_JOURNAL) {
        fprintf(stderr, "ERROR: Block %llu is not a journal block! type=0x%x\n", journal_block, *block_type);
        // Don't proceed - journal is corrupted
        return;
    }

    prev_entry = (journal_entry_t*)(block_type + 1);

    if (!prev_entry->synced) {
        printf("Found existing journal entry, syncing...\n");
        sync_entry(disk, cache, prev_entry);
    }

    if (sb.journal_head == ( calculate_journal_size(&sb) - 2 ) ) sb.journal_head = 0;
    else sb.journal_head++;

    superblock_write(disk, cache, &sb, true);

    memcpy(prev_entry, entry, sizeof(struct journal_entry_t));
    pthread_mutex_lock(get_lock());
    disk_write_block(disk, journal_block, block_type);
    pthread_mutex_unlock(get_lock());
    printf("Journal entry written, new head = %llu\n", sb.journal_head);
}

void sync_entry(DiskInterface *disk, cache *cache, journal_entry_t *entry)
{
    Inode node;
    printf("Syncing_journal_entry called with type: ");
    switch (entry->type)
    {
        case UNINITIALIZED:
            printf("UNINITIALIZED\n");
            break;
        case MKNOD:
            printf("MKNOD\n");
	    
	    if (!entry->mknod.inode_number)
            {
            	_mknod(disk, cache, entry->mknod.path, entry->mknod.mode, entry->mknod.btree_block, true, &entry->mknod.inode_number);
            	return;
            }
            inode_read(disk, cache, entry->mknod.inode_number, &node);
            node.creation_time = time(NULL);
    	    node.mode = entry->mknod.mode;
    	    inode_write(disk, cache, &node, true);
    	    
    	    char *parent = parent_path(entry->mknod.path, count_l(entry->mknod.path));
	    char *name = get_name(entry->mknod.path);
	    
            InodeBtreePair *pair = item_search(disk, cache, parent);
            
            uint64_t block_num = btree_search(disk, cache, pair->btree_block, path_hash(name));
            
            BTreeNode tree_node;
            
            if ( !block_num )
    	    {
    	        if ( FILE_TYPE_DIRECTORY == ( entry->mknod.mode & S_IFMT) )
    		{
    		        BTreeNode *second_tree_node = btree_node_create(disk, cache, false, &block_num);
    		        if (block_num)
    		        {
	    		        btree_insert(disk, cache, pair->btree_block, path_hash(name), block_num, ( entry->mknod.mode & S_IFMT));
	    		        
	    		        btree_node_read(disk, cache, block_num, &tree_node);
	    		        
	    		        tree_node.type = FILE_TYPE_DIRECTORY;
	    		        
	    		        tree_node.value = entry->mknod.inode_number;
	    		        
	    		        btree_node_write(disk, cache, &tree_node);
	    		}
	    	        else
	    	    	    goto clear;
    		}
    		else
    		{
    		    btree_insert(disk, cache, pair->btree_block, path_hash(name), entry->mknod.inode_number, ( entry->mknod.mode & S_IFMT));
    		}
    	    }
    	    else
    	    {
    	    	btree_node_read(disk, cache, block_num, &tree_node);
    		if ( FILE_TYPE_DIRECTORY == tree_node.type)
    		{
    		    tree_node.value = entry->mknod.btree_block;
	    	    btree_node_write(disk, cache, &tree_node);
    		    
    		    btree_node_read(disk, cache, entry->mknod.btree_block, &tree_node);
    		    if (tree_node.is_leaf)
    		    {
    		    	btree_node_read(disk, cache, block_num, &tree_node);
    		    	uint64_t page = 0;
    		        BTreeNode *second_tree_node = btree_node_create(disk, cache, false, &page);
    		        if (page)
	    		    tree_node.value = page;
	    		else goto clear;
	    	        btree_node_write(disk, cache, &tree_node);
	    	        btree_node_read(disk, cache, page, &tree_node);
    		    }
    		    tree_node.type = FILE_TYPE_DIRECTORY;
    		    tree_node.value = entry->mknod.inode_number;
    		}
    		else
    		    tree_node.value = entry->mknod.inode_number;
    		btree_node_write(disk, cache, &tree_node);
    	    }
    	    btree_write(disk, cache, pair->btree_block);
clear:
    	    arc4random_buf(pair, sizeof(struct InodeBtreePair));
            free(pair);
            arc4random_buf(parent, strlen(parent));
	    arc4random_buf(name, strlen(name));
	    free(parent);
	    free(name);
            break;
        case UNLINK:
            printf("UNLINK\n");
            _unlink(disk, cache, entry->unlink.path, true);
            break;
        case LINK:
            printf("LINK\n");
            _link(disk, cache, entry->link.from, entry->link.to, true);
            break;
        case CHMOD:
            printf("CHMOD\n");
            _chmod(disk, cache, entry->chmod.path, entry->chmod.mode, true);
            break;
        case TRUNCATE:
            printf("TRUNCATE\n");
            _truncate(disk, cache, entry->truncate.path, entry->truncate.size, true);
            break;
        case WRITE:
            printf("WRITE\n");
            inode_read(disk, cache, entry->write.inode_number, &node);
            inode_set_block(disk, cache, &node, entry->write.block_index, entry->write.physical_block);
            inode_write(disk, cache, &node, true);
            break;
        case RENAME:
            printf("RENAME\n");
            _rename(disk, cache, entry->rename.from, entry->rename.to, true);
            break;
    }
    entry->synced = true;
    
    block_type_t *block_type;
    block_type = ( (block_type_t*) entry - 1 );
    pthread_mutex_lock(get_lock());
    disk_write_block(disk, entry->block_number, block_type);
    pthread_mutex_unlock(get_lock());
}

void mark_entry_synced(DiskInterface *disk, cache *cache, journal_entry_t *entry)
{
    entry->synced = true;
    
    block_type_t *block_type;
    block_type = ( (block_type_t*) entry - 1 );
    pthread_mutex_lock(get_lock());
    disk_write_block(disk, entry->block_number, block_type);
    pthread_mutex_unlock(get_lock());
}

void sync_journal(DiskInterface *disk, cache *cache)
{
    Superblock sb;
    journal_entry_t *prev_entry;
    block_type_t *block_type;

    superblock_read(disk, cache, &sb);

    for (int i = 0; i < calculate_journal_size(&sb) - 1; i++)
    {
        block_type = (block_type_t*)get_block(disk, cache, 0, sb.journal_start + sb.journal_head);
        prev_entry = (journal_entry_t*)(block_type + 1);

        if (*block_type != BLOCK_TYPE_JOURNAL) {
            fprintf(stderr, "ERROR: Block %llu is not a journal block! type=0x%x\n", sb.journal_start + sb.journal_head, *block_type);
            // Don't proceed - journal is corrupted
            return;
        }

        if (prev_entry->type != UNINITIALIZED && !prev_entry->synced) {
            printf("Found existing journal entry, syncing...\n");
            sync_entry(disk, cache, prev_entry);
        }

        if (sb.journal_head == ( calculate_journal_size(&sb) - 2 ) ) sb.journal_head = 0;
        else sb.journal_head++;
        superblock_write(disk, cache, &sb, true);
    }
}

void mark_journal_synced(DiskInterface *disk, cache *cache)
{
    Superblock sb;
    journal_entry_t *prev_entry;
    block_type_t *block_type;

    superblock_read(disk, cache, &sb);

    for (int i = 0; i < calculate_journal_size(&sb) - 1; i++)
    {
        block_type = (block_type_t*)get_block(disk, cache, 0, sb.journal_start + sb.journal_head);
        prev_entry = (journal_entry_t*)(block_type + 1);

        if (*block_type != BLOCK_TYPE_JOURNAL) {
            fprintf(stderr, "ERROR: Block %llu is not a journal block! type=0x%x\n", sb.journal_start + sb.journal_head, *block_type);
            // Don't proceed - journal is corrupted
            return;
        }

        if (prev_entry->type != UNINITIALIZED && !prev_entry->synced) {
            printf("Found existing journal entry, marking as synced...\n");
            mark_entry_synced(disk, cache, prev_entry);
        }

        if (sb.journal_head == ( calculate_journal_size(&sb) - 2 ) ) sb.journal_head = 0;
        else sb.journal_head++;
        superblock_write(disk, cache, &sb, true);
    }
}
