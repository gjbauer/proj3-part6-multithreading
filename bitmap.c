#include "bitmap.h"
#include "config.h"

/**
 * Get the value of a bit at the specified index
 * Uses 64-bit words for efficient access
 */
int bitmap_get(void* bm, int ii) {
	uint64_t* ptr;
	block_type_t *block_type = (block_type_t*)bm;
	if (*block_type != BLOCK_TYPE_BITMAP) return -1;
	ptr = (uint64_t*)( (block_type_t*) bm + 1);
	ptr = ptr + ( ii / 64 );  // Find the 64-bit word containing our bit
	return (*ptr & ((uint64_t)1 << (ii % 64))) >> (ii % 64);  // Extract the bit
}

/**
 * Set or clear a bit at the specified index
 * Uses bitwise operations for efficient manipulation
 */
int bitmap_put(void* bm, int ii, int vv) {
	uint64_t* ptr;
	block_type_t *block_type = (block_type_t*)bm;
	if (*block_type != BLOCK_TYPE_BITMAP) return -1;
	ptr = (uint64_t*)( (block_type_t*) bm + 1);
	ptr = ptr + ( ii / 64 );  // Find the 64-bit word containing our bit
	// Clear bit if vv==0, set bit otherwise
	*ptr = (vv==0) ? *ptr & ~((uint64_t)1 << (ii % 64)) : *ptr | ((uint64_t)1 << (ii % 64));
	return 0;
}

/**
 * Print the bitmap for debugging purposes
 * Shows each bit as 0 or 1
 */
void bitmap_print(DiskInterface *disk, void* bm, cache *cache) {
	block_type_t *block_type = (block_type_t*)bm;
	if (*block_type != BLOCK_TYPE_BITMAP) return;
	bm = (uint64_t*)( (block_type_t*) bm + 1);
	printf("===BITMAP START===\n");
	for (int ii = 0, i=0; BLOCK_TYPE_BITMAP == *block_type; ii++, i++) {
		printf("%d", bitmap_get(bm, i));
		if ( !(ii % USABLE_BLOCK_SIZE) && ii )
		{
			bm = get_block(disk, cache, 0, (ii / USABLE_BLOCK_SIZE) );
			i=0;
		}
		block_type = (block_type_t*)bm;
	}
	printf("\n===BITMAP END===\n");
}

