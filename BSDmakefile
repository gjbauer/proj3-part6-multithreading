CFLAGS = -I/usr/local/include -L/usr/local/lib -lsysinfo -g
FUSE_FLAGS = -lfuse -D_FILE_OFFSET_BITS=64
RM_FILES = my.img mkfs.nbtrfs fuse mnt test.log
# Cache files are optimized out of compilation for mkfs & for builds with CACHE_DISABLED macro...
COMMON_FILES = bitmap.c btr.c cache.c disk.c dl.c fl.c gdl.c hash.c lru.c pci.c superblock.c inode.c string.c directory.c

all: fuse

fuse:
	clang $(CFLAGS) -o fuse $(COMMON_FILES) fuse.c $(FUSE_FLAGS)
	dd if=/dev/zero of=my.img bs=1M count=2

sanitize:
	clang $(CFLAGS) -fsanitize=address -o fuse $(COMMON_FILES) fuse.c $(FUSE_FLAGS)
	dd if=/dev/zero of=my.img bs=1M count=2

format: mkfs fuse
	./mkfs.nbtrfs my.img
	
mount:
	./fuse -s -f mnt my.img

unmount:
	sudo umount mnt
	
mkfs:
	clang $(CFLAGS) -o mkfs.nbtrfs $(COMMON_FILES) mkfs.c -DCACHE_DISABLED
	dd if=/dev/zero of=my.img bs=1M count=2

clean:
	rm -rf $(RM_FILES)

open:
	nvim -p *.h *.c

.PHONY: clean open
