#include "metadata-api.h"
#include "btr.h"
#include "directory.h"
#include "hash.h"

int _mknod(DiskInterface *disk, cache *cache, const char *path, mode_t mode, uint64_t btree_block, bool write_through, int64_t *out_inode)
{
    int rv = -1;
    char *parent = parent_path(path, count_l(path));
    char *name = get_name(path);
    Inode node;
    
    rv = inode_allocate(disk, cache, mode, true);
    if (-1 == rv) goto print;
    rv = inode_read(disk, cache, rv, &node);
    if (rv) goto print;
    if (out_inode) *out_inode = node.inode_number;
    node.creation_time = time(NULL);
    node.mode = mode;
    rv = inode_write(disk, cache, &node, write_through);
    if (rv) goto print;
    rv = directory_add_entry(disk, cache, parent, name, node.inode_number, ( mode & S_IFMT), write_through);
print:
    arc4random_buf(&node, sizeof(struct Inode));
    arc4random_buf(parent, sizeof(strlen(parent)));
    arc4random_buf(name, sizeof(char)*strlen(name));
    free(parent);
    free(name);
    printf("mknod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

int _unlink(DiskInterface *disk, cache *cache, const char *path, bool write_through)
{
    int rv = 0;
    char *parent = parent_path(path, count_l(path));
    char *name = get_name(path);
    InodeBtreePair *pair = item_search(disk, cache, parent);
    
    if (directory_exists_entry(disk, cache, parent, name, NULL))
    {
        directory_remove_entry(disk, cache, parent, name, write_through);
    }
    if (btree_search(disk, cache, pair->btree_block, path_hash(name)))
    {
    	btree_delete(disk, cache, pair->btree_block, path_hash(name));
    }
    
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    free(pair);
    arc4random_buf(parent, sizeof(char)*strlen(parent));
    arc4random_buf(name, sizeof(char)*strlen(name));
    free(parent);
    free(name);
    printf("unlink(%s) -> %d\n", path, rv);
    return rv;
}

int _link(DiskInterface *disk, cache *cache, const char *from, const char *to, bool write_through)
{
    int rv = -1;
    InodeBtreePair *from_pair = item_search(disk, cache, from);
    Inode inode;
    inode_read(disk, cache, from_pair->inode_number, &inode);
    char *parent = parent_path(to, count_l(to));
    char *name = get_name(to);
    
    DirEntry stack_entry;
    
    if (directory_exists_entry(disk, cache, parent, name, &stack_entry))
    {
        InodeBtreePair *pair = item_search(disk, cache, parent);
    	inode.reference_count++;
        inode_write(disk, cache, &inode, true);
    	if (!btree_search(disk, cache, pair->btree_block, path_hash(name)))
    	{
    		btree_insert(disk, cache, pair->btree_block, path_hash(name), stack_entry.inode_number, ( inode.mode & S_IFMT));
    	}
    	btree_write(disk, cache, pair->btree_block);
    	arc4random_buf(pair, sizeof(struct InodeBtreePair));
        free(pair);
    	arc4random_buf(from_pair, sizeof(struct InodeBtreePair));
        free(from_pair);
        arc4random_buf(&inode, sizeof(struct Inode));
        arc4random_buf(parent, strlen(parent));
        arc4random_buf(name, strlen(name));
        free(parent);
        free(name);
        return 0;
    }
    char *to_parent = parent_path(to, count_l(to));
    char *to_name = get_name(to);
    rv = directory_add_entry(disk, cache, to_parent, to_name, from_pair->inode_number, (FileType) ( inode.mode & S_IFMT), write_through);
    if (!rv)
    {
        inode.reference_count++;
        inode_write(disk, cache, &inode, write_through);
    }
    arc4random_buf(&inode, sizeof(struct Inode));
    arc4random_buf(parent, strlen(parent));
    arc4random_buf(name, strlen(name));
    free(parent);
    free(name);
    arc4random_buf(from_pair, sizeof(struct InodeBtreePair));
    arc4random_buf(to_parent, sizeof(char)*strlen(to_parent));
    arc4random_buf(to_name, sizeof(char)*strlen(to_name));
    free(from_pair);
    free(to_parent);
    free(to_name);
    printf("link(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

int _chmod(DiskInterface *disk, cache *cache, const char *path, mode_t mode, bool write_through)
{
    int rv = -1;
    InodeBtreePair *pair = item_search(disk, cache, path);
    Inode node;
    rv = inode_read(disk, cache, pair->inode_number, &node);
    if (rv) return rv;
    node.mode = mode;
    rv = inode_write(disk, cache, &node, write_through);
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    arc4random_buf(&node, sizeof(struct Inode));
    free(pair);
    printf("chmod(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

int _truncate(DiskInterface *disk, cache *cache, const char *path, off_t size, bool write_through)
{
    int rv = 0;
    InodeBtreePair *pair = item_search(disk, cache, path);
    Inode inode;
    inode_read(disk, cache, pair->inode_number, &inode);

    if (size < inode.size) {
        // Need to free blocks - but this should be idempotent
        // Calculate which blocks to free based on new vs old size
        uint64_t old_blocks = (inode.size + USABLE_BLOCK_SIZE - 1) / USABLE_BLOCK_SIZE;
        uint64_t new_blocks = (size + USABLE_BLOCK_SIZE - 1) / USABLE_BLOCK_SIZE;

        for (uint64_t i = new_blocks; i < old_blocks; i++) {
            uint64_t physical_block = 0;
            if (!inode_get_block(disk, cache, &inode, i, &physical_block) && physical_block) {
                free_page(disk, cache, physical_block);
                inode_set_block(disk, cache, &inode, i, 0);
            }
        }
    }

    inode.size = size;
    inode_write(disk, cache, &inode, write_through);
    
    arc4random_buf(&inode, sizeof(struct Inode));
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    free(pair);
    // TODO: Free empty indirect and double-indirect blocks when empty
    printf("truncate(%s, %lld bytes) -> %d\n", path, size, rv);
    return rv;
}

int _rename(DiskInterface *disk, cache *cache, const char *from, const char *to, bool write_through)
{
    int rv = -1;
    char *to_parent = parent_path(to, count_l(to));
    char *to_name = get_name(to);
    InodeBtreePair *pair = item_search(disk, cache, to_parent);
    if (!btree_search(disk, cache, pair->btree_block, path_hash(to_name)))
    {
    	rv = _link(disk, cache, from, to, write_through);
    	if (rv) goto cleanup;
    	rv = _unlink(disk, cache, from, write_through);
    }
cleanup:
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    free(pair);
    arc4random_buf(to_name, strlen(to_name)*sizeof(char));
    free(to_name);
    arc4random_buf(to_parent, strlen(to_parent)*sizeof(char));
    free(to_parent);
    printf("rename(%s => %s) -> %d\n", from, to, rv);
    return rv;
}
