
#include <sm/sm.h>
#include <sm/mmu.h>
#include <sm/bitmap.h>
#include <sm/reverse_map.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_unpriv.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/riscv_locks.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_pmp.h>
#include <sbi/sbi_tvm.h>
#include <sbi/sbi_math.h>

// Set Mem region to private:
// 1. lock_bitmap()
// 2. unmap_range()
// 3. set_private_range()
// 4. unlock_bitmap()

//#define lock_bitmap spin_lock(&bitmap_lock);
//#define unlock_bitmap spin_unlock(&bitmap_lock);

/**
 * Invalidate all PTEs in HPT Area that point to the range of physical pages (not atomic).
 * @param pfn_start The start page frame
 * @param num The number of pages in the pfn range
 * @return 0 on success, negative error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
//int unmap_range(uint64_t pfn_start, uint64_t num);

/**
 * Set a range of physical pages, [pfn, pfn + pagenum) to secure pages (not atomic).
 * This function only updates the metadata of physical pages without unmapping
 * them in the host PT pages. (should call unmap_range before/after this function)
 * Also, the function will not check whether a page is already secure.
 * The caller of the function should be careful to perform the above two tasks.
 *
 * @param pfn_start The start page frame
 * @param num The number of pages in the pfn range
 * @return 0 on success, error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
//int set_private_range(uint64_t pfn_start, uint64_t num);

int set_private(uint64_t pfn_start, uint64_t num)
{
	int ret = 0; 
    lock_bitmap;
    if (unmap_range(pfn_start, num))
    {
        ret = -1;
        goto out;

    }
	if (set_private_range(pfn_start, num))
    {
        ret = -1;
        goto out;
    }

out:
    unlock_bitmap;
	return ret; 
}

void print_state(ulong addr, ulong tval2, ulong tinst,
		       struct sbi_trap_regs *regs)
{

	sbi_printf("sbi_access_handler: addr: %lx, tval2: %lx, tinst: %lx\n",
		   addr, tval2, tinst);

	sbi_printf(
		"a0: %lx, a1: %lx, a2: %lx, a3: %lx, a4: %lx, a5: %lx, a6: %lx, a7: %lx\n",
		regs->a0, regs->a1, regs->a2, regs->a3, regs->a4, regs->a5,
		regs->a6, regs->a7);
	sbi_printf(
		"s0: %lx, s1: %lx, s2: %lx, s3: %lx, s4: %lx, s5: %lx, s6: %lx, s7: %lx\n",
		regs->s0, regs->s1, regs->s2, regs->s3, regs->s4, regs->s5,
		regs->s6, regs->s7);
	sbi_printf(
		"s8: %lx, s9: %lx, s10: %lx, s11: %lx, t0: %lx, t1: %lx, t2: %lx, t3: %lx\n",
		regs->s8, regs->s9, regs->s10, regs->s11, regs->t0, regs->t1,
		regs->t2, regs->t3);
	sbi_printf(
		"t4: %lx, t5: %lx, t6: %lx, tp: %lx, gp: %lx, sp: %lx, ra: %lx\n",
        regs->t4, regs->t5, regs->t6, regs->tp, regs->gp, regs->sp,
        regs->ra);
	sbi_printf("mepc: %lx, mstatus: %lx, mstatusH: %lx, extraInfo: %lx\n",
		   regs->mepc, regs->mstatus, regs->mstatusH, regs->extraInfo);
}

int sbi_access_handler(ulong addr, ulong tval2, ulong tinst,
		       struct sbi_trap_regs *regs)
{ 
    print_state(addr, tval2, tinst, regs);

    // get page allocation from kernel 

    int ret;

    get_kernel_page();
    
    // set the page to private


    // update pte
    int ret = check_set_single_pte(phaddr, pte, 
					   get_page_num((uintptr_t)addr));


	return 0;
}
