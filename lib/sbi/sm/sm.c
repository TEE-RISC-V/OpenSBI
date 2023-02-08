#include <sm/sm.h>
#include <sm/bitmap.h>
#include <sbi/sbi_console.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_pmp.h>
#include <sbi/sbi_tvm.h>
#include <sbi/sbi_math.h>

// TODO: more levels
#include <sbi/sbi_bitops.h>
#if __riscv_xlen == 64
#define SATP_MODE_CHOICE INSERT_FIELD(0, SATP64_MODE, SATP_MODE_SV39)
#else
#define SATP_MODE_CHOICE INSERT_FIELD(0, SATP32_MODE, SATP_MODE_SV32)
#endif
#define IS_PGD(pte) (pte & SATP_MODE_CHOICE)

inline uintptr_t pte_to_ppn(uintptr_t pte)
{
	return pte >> PTE_PPN_SHIFT;
}
inline uintptr_t pte_to_phys(uintptr_t pte)
{
	return pte_to_ppn(pte) << PAGE_SHIFT;
}

unsigned int next_pmp_idx;
void update_min_usable_pmp_id(unsigned int pmp_idx)
{
	next_pmp_idx = pmp_idx;
}

void sm_init()
{
	// TODO: set up initial PMP registers
	sbi_printf("\nSM Init\n\n");
}

bool check_enabled  = false;
uintptr_t hpt_start = 0, hpt_end = 0;
uintptr_t hpt_pmd_start = 0;
uintptr_t hpt_pte_start = 0;

int bitmap_and_hpt_init(uintptr_t bitmap_start, uint64_t bitmap_size,
			uintptr_t hpt_start_, uint64_t hpt_size,
			uintptr_t hpt_pmd_start_, uintptr_t hpt_pte_start_)
{
	sbi_printf(
		"bitmap_and_hpt_init: bitmap_start: 0x%lx, bitmap_size: 0x%lx, hpt_start: 0x%lx, hpt_size: 0x%lx, hpt_pmd_start: 0x%lx, hpt_pte_start: 0x%lx\n",
		(uint64_t)bitmap_start, bitmap_size, (uint64_t)hpt_start_,
		hpt_size, (uint64_t)hpt_pmd_start_, (uint64_t)hpt_pte_start_);

	int r;

	hpt_start     = hpt_start_;
	hpt_end	      = hpt_start + hpt_size;
	hpt_pmd_start = hpt_pmd_start_;
	hpt_pte_start = hpt_pte_start_;

	r = init_bitmap(bitmap_start, bitmap_size);
	if (r) {
		sbi_printf(
			"bitmap_and_hpt_init: bitmap init failed (error %d)\n",
			r);
		return r;
	}

	r = set_pmp_and_sync(next_pmp_idx++, 0, bitmap_start,
			     log2roundup(bitmap_size));
	if (r) {
		sbi_printf(
			"bitmap_and_hpt_init: PMP for bitmap init failed (error %d)\n",
			r);
		return r;
	}

	r = set_pmp_and_sync(next_pmp_idx++, 0, hpt_start,
			     log2roundup(hpt_size));
	if (r) {
		sbi_printf(
			"bitmap_and_hpt_init: PMP for HPT Area init failed (error %d)\n",
			r);
		return r;
	}

	sbi_printf("PMP set up for bitmap and HPT Area\n");

	return 0;
}

int monitor_init(uintptr_t *mstatus)
{
	// ensure PGD entries only map to HPT PMD Area
	for (uintptr_t *pte = (uintptr_t *)hpt_start;
	     pte < (uintptr_t *)hpt_pmd_start; pte++) {
		if (!IS_PGD(*pte) && (*pte & PTE_V)) {
			uintptr_t nxt_pt = pte_to_phys(*pte);

			if ((nxt_pt < hpt_pmd_start) ||
			    (nxt_pt >= hpt_pte_start)) {
				sbi_printf(
					"[%s] Invalid PGD entry(0x%lx): 0x%lx (a mapping to address 0x%lx), should be in [0x%lx, 0x%lx)\n",
					__func__, (uintptr_t)pte, *pte, nxt_pt,
					hpt_pmd_start, hpt_pte_start);
				return -1;
			}
		}
	}

	// check non-leaf PMD entries only map to HPT PTE Area
	for (uintptr_t *pte = (uintptr_t *)hpt_pmd_start;
	     pte < (uintptr_t *)hpt_pte_start; pte++) {
		if ((*pte & PTE_V) && !(*pte & PTE_R) && !(*pte & PTE_W) &&
		    !(*pte & PTE_X)) {
			uintptr_t nxt_pt = pte_to_phys(*pte);
			if ((nxt_pt < hpt_pte_start) || (nxt_pt >= hpt_end)) {
				sbi_printf(
					"[%s] Invalid PMD entry(0x%lx): 0x%lx (a mapping to address 0x%lx), should be in [0x%lx, 0x%lx)\n",
					__func__, (uintptr_t)pte, *pte, nxt_pt,
					hpt_pte_start, hpt_end);
				return -1;
			}
		}
	}

	set_tvm_and_sync();
	*mstatus = csr_read(CSR_MSTATUS);

	check_enabled = true;
	sbi_printf("\nSM Monitor Init\n\n");
	return 0;
}

int sm_set_shared(uintptr_t paddr_start, uint64_t size)
{
	// TODO: replace this with real implementation
	sbi_printf("sm_set_shared(0x%lx, 0x%lx) is called\n", paddr_start,
		   size);
	return 0;
}

int get_page_num(uintptr_t pte_addr)
{
	if (unlikely(((hpt_start) <= pte_addr) && ((hpt_pmd_start) > pte_addr)))
		return 512 * 512;
	if (unlikely(((hpt_pmd_start) <= pte_addr) &&
		     ((hpt_pte_start) > pte_addr)))
		return 512;
	if (likely((hpt_pte_start <= pte_addr) && (pte_addr < hpt_end)))
		return 1;
	return -1;
}

/**
 * @brief Set the PTE
 *
 * @param addr physical address of the entry
 * @param pte the new value of the entry
 * @param page_num The number of pages to be set
 * @return 0 on success, negative error code on failure
 */
inline int set_single_pte(unsigned long *addr, unsigned long pte, size_t page_num)
{
	*((unsigned long *)addr) = pte;
	return 0;
}

/**
 * @brief Check if the action is valid, then perform it
 *
 * @param addr physical address of the entry
 * @param pte the new value of the entry
 * @param page_num The number of pages to be set
 * @return 0 on success, negative error code on failure
 */
inline int check_set_single_pte(unsigned long *addr, unsigned long pte,
			 size_t page_num)
{
	if (unlikely(check_enabled == false)) {
		set_single_pte(addr, pte, page_num);
		return 0;
	}
	if (page_num < 0) {
		sbi_printf(
			"sm_set_pte: addr outside HPT (addr: 0x%lx, pte: 0x%lx, page_num: %lu)\n",
			(unsigned long)addr, pte, page_num);
		return -1;
	}
	if (pte & PTE_V) {
		if (page_num == 512 * 512) { // PGD
			uintptr_t nxt_pt = pte_to_phys(pte);
			if ((nxt_pt < hpt_pmd_start) ||
			    (nxt_pt >= hpt_pte_start)) {
				sbi_printf(
					"[%s] Invalid PGD entry(0x%lx): 0x%lx (a mapping to address 0x%lx), should be in [0x%lx, 0x%lx)\n",
					__func__, (uintptr_t)addr, pte, nxt_pt,
					hpt_pmd_start, hpt_pte_start);
				return -1;
			}
		} else if (page_num == 512 && !(pte & PTE_R) &&
			   !(pte & PTE_W) && !(pte & PTE_X)) { // non-leaf PMD
			uintptr_t nxt_pt = pte_to_phys(pte);
			if ((nxt_pt < hpt_pte_start) || (nxt_pt >= hpt_end)) {
				sbi_printf(
					"[%s] Invalid PMD entry(0x%lx): 0x%lx (a mapping to address 0x%lx), should be in [0x%lx, 0x%lx)\n",
					__func__, (uintptr_t)addr, pte, nxt_pt,
					hpt_pte_start, hpt_end);
				return -1;
			}
		} else { // leaf
			if (!test_public_shared_range(pte_to_ppn(pte),
						      page_num)) {
				sbi_printf(
					"Invalid page table leaf entry, contains private range(addr 0x%lx, pte 0x%lx, page_num %ld)\n",
					(uintptr_t)addr, pte, page_num);
				return -1;
			}
		}
	}

	return set_single_pte(addr, pte, page_num);
}

int sm_set_pte(unsigned long sub_fid, unsigned long *addr,
	       unsigned long pte_or_src, size_t size)
{
	// TODO: lock?
	int ret = 0;
	switch (sub_fid) {
	case SBI_EXT_SM_SET_PTE_CLEAR:
		for (size_t i = 0; i < size / sizeof(uintptr_t); ++i, ++addr) {
			set_single_pte(addr, 0, 0);
		}
		break;
	case SBI_EXT_SM_SET_PTE_MEMCPY:
		if (size % 8) {
			ret = -1;
			sbi_printf(
				"sm_set_pte: SBI_EXT_SM_SET_PTE_MEMCPY: size align failed (addr: 0x%lx, src: 0x%lx, size: %lu)\n",
				(unsigned long)addr, pte_or_src, size);
		}
		size_t page_num = size >> 3;
		for (size_t i = 0; i < page_num; ++i, ++addr) {
			uintptr_t pte = *((uintptr_t *)pte_or_src + i);
			ret	      = check_set_single_pte(
				  addr, pte, get_page_num((uintptr_t)addr));
			if (unlikely(ret))
				break;
		}
		break;
	case SBI_EXT_SM_SET_PTE_SET_ONE:
		ret = check_set_single_pte(addr, pte_or_src,
					   get_page_num((uintptr_t)addr));
		break;
	default:
		ret = -1;
		break;
	}
	return ret;
}
