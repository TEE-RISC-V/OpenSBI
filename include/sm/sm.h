#ifndef __SM_H__
#define __SM_H__

#include <sbi/sbi_types.h>
#include <sbi/sbi_trap.h>

void sm_init();

void update_min_usable_pmp_id(unsigned int pmp_idx);

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
 * @param hpt_pmd_start The start address of the PMD section of HPT Area
 * @param hpt_pte_start The start address of the PTE section of HPT Area
 * @return 0 on success, negative error code on failure
 */
int bitmap_and_hpt_init(uintptr_t bitmap_start, uint64_t bitmap_size,
			uintptr_t hpt_start, uint64_t hpt_size,
			uintptr_t hpt_pmd_start, uintptr_t hpt_pte_start);

/**
 * Enable monitoring HPT Area.
 * Also ensure that page tables in HPT Area only have entries inside HPT Area
 *
 * @return 0 on success, negative error code on failure
 */
int monitor_init();

/**
 * Set a contiguous memory region as shared region
 *
 * @param paddr_start physical address of the start of the region
 * @param size size of the region
 * @return 0 on success, negative error code on failure
 */
int sm_set_shared(uintptr_t paddr_start, uint64_t size);

/**
 * Saves CPU details into global memory
 *
 * @param regs the registers to save
 * @param trap the trap information to save
 * @return 0 on success, error code on failure
 */
int sm_preserve_cpu(struct sbi_trap_regs *regs, struct sbi_trap_info *trap);


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
int sm_resume_cpu(uint64_t cpu_id, struct sbi_trap_regs *regs);

/*
 * Set pte entry, check if the action is valid
 * 
 * @param sub_fid sub-function id, SBI_EXT_SM_SET_PTE_*
 * @param addr physical address of the entry / destination physical address
 * @param pte_or_src the new value of the entry / source physical address
 * @param size the size of the entries
 * @return 0 on success, negative error code on failure
 */
int sm_set_pte(unsigned long sub_fid, unsigned long *addr,
	       unsigned long pte_or_src, size_t size);

#endif
