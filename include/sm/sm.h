#ifndef __SM_H__
#define __SM_H__

#include <sbi/sbi_types.h>

void sm_init();

/**
 * Initialize the bitmap and HPT Area.
 * 1. Initialize the data structure.
 * 2. Check page tables in HPT Area.
 * 3. Set up the PMP.
 *
 * @param bitmap_start The start address of the bitmap
 * @param bitmap_size The bitmap memory size
 * @param hpt_start The start address of the HPT Area
 * @param hpt_size The HPT Area size
 * @return 0 on success, negative error code on failure
 */
int bitmap_and_hpt_init(uintptr_t bitmap_start, uint64_t bitmap_size,
			uintptr_t hpt_start, uint64_t hpt_size);

/**
 * Set a contiguous memory region as shared region
 *
 * @param paddr_start physical address of the start of the region
 * @param size size of the region
 * @return 0 on success, negative error code on failure
 */
int sm_set_shared(uintptr_t paddr_start, uint64_t size);

#endif
