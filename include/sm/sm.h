#ifndef __SM_H__
#define __SM_H__

#include <sbi/sbi_types.h>

void sm_init();

/**
 * Set a contiguous memory region as shared region
 *
 * @param paddr_start physical address of the start of the region
 * @param size size of the region
 * @return 0 on success, error code on failure
 */
int sm_set_shared(uint64_t paddr_start, uint64_t size);

#endif
