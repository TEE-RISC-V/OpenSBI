#ifndef __SM_H__
#define __SM_H__

#include <sbi/sbi_types.h>
#include <sbi/sbi_trap.h>

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
 * @param cpu_id id of the vCPU
 * @return 0 on success, error code on failure
 */
int sm_prepare_cpu(uint64_t cpu_id);

/**
 * Preserve a CPU (TODO: Details)
 *
 * @param cpu_id id of the vCPU
 * @return 0 on success, error code on failure
 */
int sm_preserve_cpu(uint64_t cpu_id);


/**
 * Create a CPU (TODO: Details)
 *
 * @param cpu_id id of the vCPU
 * @return 0 on success, error code on failure
 */
int sm_create_cpu(uint64_t cpu_id, const struct sbi_trap_regs *regs);

/**
 * Resume a CPU (TODO: Details)
 *
 * @param cpu_id id of the vCPU
 * @return 0 on success, error code on failure
 */
int sm_resume_cpu(uint64_t cpu_id, const struct sbi_trap_regs *regs);

#endif
