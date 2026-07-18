#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <errno.h>
#include <sys/stat.h>
#include <dirent.h>
#ifdef __linux__
#include <bsd/stdlib.h>
#endif
#include <stdlib.h>
#include <assert.h>

#define FUSE_USE_VERSION 26
#include <fuse.h>

#include "hash.h"
#include "inode.h"
#include "string.h"
#include "directory.h"

#include "journal.h"
#include "metadata-api.h"

DiskInterface* disk;
cache *cache_s;

// implementation for: man 2 access
// Checks if a file exists.
int
nbtrfs_access(const char *path, int mask)
{
    int rv = -ENOENT;
    InodeBtreePair *pair = item_search(disk, cache_s, path);
    if (pair->inode_number || pair->btree_block) rv = 0;
    printf("access(%s, %04o) -> %d\n", path, mask, rv);
    arc4random_buf(pair, sizeof(InodeBtreePair));
    free(pair);
    return rv;
}

// implementation for: man 2 stat
// gets an object's attributes (type, permissions, size, etc)
int
nbtrfs_getattr(const char *path, struct stat *st)
{
    int rv = -ENOENT;
    InodeBtreePair *pair = item_search(disk, cache_s, path);
    Inode node;
    
    if (pair->inode_number || pair->btree_block)
    {
        inode_read(disk, cache_s, pair->inode_number, &node);
        st->st_mode = node.mode;
        st->st_size = node.size;
        st->st_uid = node.owner_id;
        st->st_gid = node.group_id;
        st->st_nlink = node.reference_count;
        st->st_ctime = node.creation_time;
        st->st_mtime = node.modification_time;
        st->st_atime = node.access_time;
        st->st_ino = pair->inode_number;
        rv = 0;
    }
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    free(pair);
    printf("getattr(%s) -> (%d) {mode: %04o, size: %lld}\n", path, rv, st->st_mode, st->st_size);
    return rv;
}

// implementation for: man 2 readdir
// lists the contents of a directory
int
nbtrfs_readdir(const char *path, void *buf, fuse_fill_dir_t filler,
             off_t offset, struct fuse_file_info *fi)
{
    struct stat st;
    int rv, count;
    int l = count_l(path);
    DirEntry *entries;
    char absolute[PATH_MAX];
    memset(absolute, '\0', PATH_MAX);

    rv = nbtrfs_getattr(path, &st);
    assert(rv == 0);

    filler(buf, ".", &st, 0);
    
    if (l > 0)
    {
        char *parent = parent_path(path, l);
        rv = nbtrfs_getattr(parent, &st);
        free(parent);
        assert(rv == 0);

        filler(buf, "..", &st, 0);
    }
    
    count = directory_list(disk, cache_s, path, &entries);
    for (int i=0; i<count; i++)
    {
        if (count_l(path) > 0) snprintf(absolute, PATH_MAX, "%s/%s", path, entries[i].name);
        else snprintf(absolute, PATH_MAX, "%s%s", path, entries[i].name);
        rv = nbtrfs_getattr(absolute, &st);
        assert(rv == 0);
        filler(buf, entries[i].name, &st, 0);
    }
    
    if (entries)
    {
        arc4random_buf(entries, count*sizeof(struct DirEntry));
        free(entries);
    }
    printf("readdir(%s) -> %d\n", path, rv);
    return 0;
}

// mknod makes a filesystem object like a file or directory
// called for: man 2 open, man 2 link
int
nbtrfs_mknod(const char *path, mode_t mode, dev_t rdev)
{
    int rv = -1;
    journal_entry_t entry;
    entry.type = MKNOD;
    entry.synced = false;
    strncpy(entry.mknod.path, path, PATH_MAX);
    entry.mknod.mode = mode;
    entry.mknod.inode_number = 0;
    rv = _mknod(disk, cache_s, path, mode, 0, false, &entry.mknod.inode_number);
    initialize_journal_entry(disk, cache_s, &entry);
    return rv;
}

int
nbtrfs_create(const char *path, mode_t mode, struct fuse_file_info *fi)
{
    // Force direct I/O on this file descriptor to bypass kernel page cache
    fi->direct_io = 1; 
    
    // Force FUSE to invalidate existing cached data pages for this file
    fi->keep_cache = 0; 
    int rv = nbtrfs_mknod(path, mode | S_IFREG, 0);
    printf("create(%s, %04o) -> %d\n", path, mode, rv);
    return rv;
}

// most of the following callbacks implement
// another system call; see section 2 of the manual
int
nbtrfs_mkdir(const char *path, mode_t mode)
{
    int rv = nbtrfs_mknod(path, mode | S_IFDIR, 0);
    printf("mkdir(%s) -> %d\n", path, rv);
    return rv;
}

int
nbtrfs_unlink(const char *path)
{
    journal_entry_t entry;
    entry.type = UNLINK;
    entry.synced = false;
    strcpy(entry.unlink.path, path);
    initialize_journal_entry(disk, cache_s, &entry);
    return _unlink(disk, cache_s, path, false);
}

int
nbtrfs_link(const char *from, const char *to)
{
    journal_entry_t entry;
    entry.type = LINK;
    entry.synced = false;
    strcpy(entry.link.from, from);
    strcpy(entry.link.to, to);
    initialize_journal_entry(disk, cache_s, &entry);
    return _link(disk, cache_s, from, to, false);
}

int
nbtrfs_rmdir(const char *path)
{
    int rv = nbtrfs_unlink(path);
    printf("rmdir(%s) -> %d\n", path, rv);
    return rv;
}

// implements: man 2 rename
// called to move a file within the same filesystem
int
nbtrfs_rename(const char *from, const char *to)
{
    journal_entry_t entry;
    entry.type = RENAME;
    entry.synced = false;
    strcpy(entry.rename.from, from);
    strcpy(entry.rename.to, to);
    initialize_journal_entry(disk, cache_s, &entry);
    
    int rv = _rename(disk, cache_s, from, to, false);
    
    printf("rename(%s => %s) -> %d\n", from, to, rv);
    return rv;
}

int
nbtrfs_chmod(const char *path, mode_t mode)
{
    journal_entry_t entry;
    entry.type = CHMOD;
    entry.synced = false;
    strcpy(entry.chmod.path, path);
    entry.chmod.mode = mode;
    initialize_journal_entry(disk, cache_s, &entry);
    return _chmod(disk, cache_s, path, mode, false);
}

int
nbtrfs_truncate(const char *path, off_t size)
{
    journal_entry_t entry;
    entry.type = TRUNCATE;
    entry.synced = false;
    strcpy(entry.truncate.path, path);
    entry.truncate.size = size;
    initialize_journal_entry(disk, cache_s, &entry);
    return _truncate(disk, cache_s, path, size, false);
}

// this is called on open, but doesn't need to do much
// since FUSE doesn't assume you maintain state for
// open files.
int
nbtrfs_open(const char *path, struct fuse_file_info *fi)
{
    int rv = 0;
    // Force direct I/O on this file descriptor to bypass kernel page cache
    fi->direct_io = 1; 
    
    // Force FUSE to invalidate existing cached data pages for this file
    fi->keep_cache = 0; 
    printf("open(%s) -> %d\n", path, rv);
    return rv;
}

// Actually read data
int
nbtrfs_read(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int rv = 0;
    InodeBtreePair *pair = item_search(disk, cache_s, path);
    Inode node;
    
    if (!pair->inode_number && !pair->btree_block)
    {
    	rv = -ENOENT;
    	goto exit;
    }
    else if (pair->btree_block)
    {
    	rv = -EISDIR;
    	goto exit;
    }
    
    rv = inode_read(disk, cache_s, pair->inode_number, &node);
    if (rv) {
        goto exit;
    }
    
    // Check if offset is beyond file size
    if (offset >= node.size) {
        goto exit; // Nothing to read
    }
    
    // Adjust size if it would read beyond the end of the file
    if (offset + size > node.size) {
        size = node.size - offset;
    }
    
    if (size == 0) {
        goto exit;
    }
    
    int bytes_read = 0;
    int remaining = size;
    off_t current_offset = offset;
    
    while (remaining > 0) {
        // Calculate which page we're reading from
        int page_index = current_offset / USABLE_BLOCK_SIZE;
        int page_offset = current_offset % USABLE_BLOCK_SIZE;
        
        // Get the physical page number for this logical page
        uint64_t pnum = 0;
        
        rv = inode_get_block(disk, cache_s, &node, page_index, &pnum);
        if (rv) {
            goto exit;
        }
        
        if (!pnum) {
            break; // No page allocated here
        }
        
        // Get pointer to the page data
        block_type_t *block_type = (block_type_t*) get_block(disk, cache_s, node.inode_number, pnum);;
        // For data blocks, we need to skip the block type header
        char *data_area = (char*)(block_type + 1);
        
        // Adjust calculations to account for block type header
        int data_offset = page_offset;
        int available_data = USABLE_BLOCK_SIZE - data_offset;
        int bytes_to_read = available_data;
        if (bytes_to_read > remaining) {
            bytes_to_read = remaining;
        }
        
        // Copy data from page to buffer (after block type header)
        memcpy(buf + bytes_read, data_area + data_offset, bytes_to_read);
        
        // Update counters
        bytes_read += bytes_to_read;
        remaining -= bytes_to_read;
        current_offset += bytes_to_read;
    }
    rv = bytes_read;
    
exit:
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    arc4random_buf(&node, sizeof(struct Inode));
    free(pair);
    printf("read(%s, %ld bytes, @+%lld) -> %d\n", path, size, offset, rv);
    return rv;
}

// Actually write data
int
nbtrfs_write(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    int bytes_written = 0;
    InodeBtreePair *pair = item_search(disk, cache_s, path);
    Inode node;
    
    if (!pair->inode_number && !pair->btree_block)
    {
        nbtrfs_mknod(path, S_IFREG | 0644, 0);
        arc4random_buf(pair, sizeof(struct InodeBtreePair));
    	free(pair);
        pair = item_search(disk, cache_s, path);
    }
    else if (pair->btree_block)
    {
    	bytes_written = -EISDIR;
    	goto exit;
    }
    
    if (inode_read(disk, cache_s, pair->inode_number, &node)) {
        goto exit;
    }
    
    int remaining = size;
    off_t current_offset = offset;
    
    while (remaining > 0) {
        // Calculate which page we're writing to
        int page_index = current_offset / USABLE_BLOCK_SIZE;
        int page_offset = current_offset % USABLE_BLOCK_SIZE;
        
        // Get the physical page number for this logical page
        uint64_t pnum = 0;
        
        inode_get_block(disk, cache_s, &node, page_index, &pnum);
        
        if (!pnum)
        {
            pnum = alloc_page(disk, cache_s);
            if (!pnum) goto exit;
            // Initialize the new page as a data block
            block_type_t *block_type = (block_type_t*)get_block(disk, cache_s, node.inode_number, pnum);
            *block_type = BLOCK_TYPE_DATA;
            // Clear the rest of the block
            memset(block_type + 1, 0, BLOCK_SIZE - sizeof(block_type_t));
            inode_set_block(disk, cache_s, &node, page_index, pnum);
            inode_write(disk, cache_s, &node, false);
            journal_entry_t entry;
            entry.type = WRITE;
            entry.write.inode_number = node.inode_number;
            entry.write.block_index = page_index;
            entry.write.physical_block = pnum;
            entry.write.size = size;
            initialize_journal_entry(disk, cache_s, &entry);
        }
        
        // Get pointer to the page data
        block_type_t *block_type = (block_type_t*) get_block(disk, cache_s, node.inode_number, pnum);
        // For data blocks, we need to skip the block type header
        char *data_area = (char*)(block_type + 1);
        
        // Calculate how much to write to this page
        int bytes_to_write = USABLE_BLOCK_SIZE - page_offset;
        if (bytes_to_write > remaining) {
            bytes_to_write = remaining;
        }
        
        // Copy data from buffer to page
        memcpy(data_area + page_offset, buf + bytes_written, bytes_to_write);
        
        // Mark the page as dirty since we modified it
        write_block(disk, cache_s, block_type, node.inode_number, pnum);
        
        // Update counters
        bytes_written += bytes_to_write;
        remaining -= bytes_to_write;
        current_offset += bytes_to_write;
    }
    
    // Update file size if we wrote beyond the previous end
    if (offset + bytes_written > node.size) {
        node.size = offset + bytes_written;
        inode_write(disk, cache_s, &node, false);
    }

exit:
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    arc4random_buf(&node, sizeof(struct Inode));
    free(pair);
    printf("write(%s, %ld bytes, @+%lld) -> %d\n", path, size, offset, bytes_written);
    return bytes_written;
}

// Update the timestamps on a file or directory.
int
nbtrfs_utimens(const char* path, const struct timespec ts[2])
{
    int rv = -1;
    InodeBtreePair *pair = item_search(disk, cache_s, path);
    Inode node;
    rv = inode_read(disk, cache_s, pair->inode_number, &node);
    if (rv) return rv;
    node.access_time = ts[0].tv_sec * 1000000000ULL + ts[0].tv_nsec;
    node.modification_time = ts[1].tv_sec * 1000000000ULL + ts[1].tv_nsec;
    rv = inode_write(disk, cache_s, &node, false);
    arc4random_buf(pair, sizeof(struct InodeBtreePair));
    arc4random_buf(&node, sizeof(struct Inode));
    free(pair);
    printf("utimens(%s, [%ld, %ld; %ld %ld]) -> %d\n",
           path, ts[0].tv_sec, ts[0].tv_nsec, ts[1].tv_sec, ts[1].tv_nsec, rv);
	return rv;
}

// Extended operations
int
nbtrfs_ioctl(const char* path, int cmd, void* arg, struct fuse_file_info* fi,
           unsigned int flags, void* data)
{
    int rv = -1;
    printf("ioctl(%s, %d, ...) -> %d\n", path, cmd, rv);
    return rv;
}

int nbtrfs_fsync(const char *path, int datasync, struct fuse_file_info *fi)
{
	InodeBtreePair *pair = item_search(disk, cache_s, path);
	cache_fsync(disk, cache_s, pair->inode_number);
	printf("fsync(%s)\n", path);
	arc4random_buf(pair, sizeof(struct InodeBtreePair));
	free(pair);
}

void nbtrfs_destroy(void *private_data)
{
    
    printf("Unmounting: Syncing data and cleaning up...\n");

    printf("Marking journal entries as synced...\n");
    mark_journal_synced(disk, cache_s);

    printf("Syncing cache...\n");
    cache_sync(disk, cache_s);
    
    printf("Unmounting: Freeing cache...\n");
    free_cache(cache_s);

    printf("Cleanup complete.\n");
}

void* 
nbtrfs_init(struct fuse_conn_info *conn)
{
    // Request atomic O_TRUNC from the kernel
    if (conn->capable & FUSE_CAP_ATOMIC_O_TRUNC) {
        conn->want |= FUSE_CAP_ATOMIC_O_TRUNC;
    }
    return NULL;
}

void
nbtrfs_init_ops(struct fuse_operations* ops)
{
    memset(ops, 0, sizeof(struct fuse_operations));
    ops->access   = nbtrfs_access;
    ops->getattr  = nbtrfs_getattr;
    ops->readdir  = nbtrfs_readdir;
    ops->mknod    = nbtrfs_mknod;
    ops->create   = nbtrfs_create;
    ops->mkdir    = nbtrfs_mkdir;
    ops->link     = nbtrfs_link;
    ops->unlink   = nbtrfs_unlink;
    ops->rmdir    = nbtrfs_rmdir;
    ops->rename   = nbtrfs_rename;
    ops->chmod    = nbtrfs_chmod;
    ops->truncate = nbtrfs_truncate;
    ops->open	  = nbtrfs_open;
    ops->read     = nbtrfs_read;
    ops->write    = nbtrfs_write;
    ops->utimens  = nbtrfs_utimens;
    ops->ioctl    = nbtrfs_ioctl;
    ops->destroy  = nbtrfs_destroy;
    ops->init     = nbtrfs_init;
    ops->fsync	  = nbtrfs_fsync;
};

struct fuse_operations nbtrfs_ops;


int
main(int argc, char *argv[])
{
    //assert(argc > 2 && argc < 6);
    //printf("TODO: mount %s as data file\n", argv[--argc]);
    //storage_init(argv[--argc]);
    disk = disk_open(argv[--argc]);
    cache_s = alloc_cache();
    sync_journal(disk, cache_s);
    nbtrfs_init_ops(&nbtrfs_ops);
    return fuse_main(argc, argv, &nbtrfs_ops, NULL);
}


/*
int main()
{
    disk = disk_open("my.img");
    cache_s = alloc_cache();
    btree_print(disk, cache_s, 8, 0);
    nbtrfs_mkdir("/hello", 0755);
    btree_print(disk, cache_s, 8, 0);
    InodeBtreePair *pair = item_search(disk, cache_s, "/hello");
    print_pair(pair);
    free(pair);
    nbtrfs_mknod("/hello.txt", 0755, 0);
    pair = item_search(disk, cache_s, "/hello");
    print_pair(pair);
    btree_print(disk, cache_s, 8, 0);
}
*/
