#ifndef __SM_H__
#define __SM_H__

#include <sbi/sbi_types.h>

int sm_init();

/**
 * Set a contiguous memory region as shared region
 *
 * @param paddr_start physical address of the start of the region
 * @param size size of the region
 * @return 0 on success, error code on failure
 */
int sm_set_shared(uint64_t paddr_start, uint64_t size);

/**
 * Prepare a CPU (TODO: Details)
 *
 * @param vm_id id of the virtual machine
 * @param cpu_id id of the vCPU
 * @return 0 on success, error code on failure
 */
int sm_prepare_cpu(uint64_t vm_id, uint64_t cpu_id);

/**
 * Preserve a CPU (TODO: Details)
 *
 * @param vm_id id of the virtual machine
 * @param cpu_id id of the vCPU
 * @return 0 on success, error code on failure
 */
int sm_preserve_cpu(uint64_t vm_id, uint64_t cpu_id);

#endif
