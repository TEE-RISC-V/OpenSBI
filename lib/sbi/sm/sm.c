#include <sm/sm.h>
#include <sm/bitmap.h>
#include <sbi/sbi_console.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_pmp.h>
#include <sbi/sbi_math.h>

// TODO: more levels
#include <sbi/sbi_bitops.h>
#define MEGAPAGE_SIZE ((uintptr_t)(RISCV_PGSIZE << RISCV_PGLEVEL_BITS))
#if __riscv_xlen == 64
#define SATP_MODE_CHOICE INSERT_FIELD(0, SATP64_MODE, SATP_MODE_SV39)
#define VA_BITS 39
#define GIGAPAGE_SIZE (MEGAPAGE_SIZE << RISCV_PGLEVEL_BITS)
#else
#define SATP_MODE_CHOICE INSERT_FIELD(0, SATP32_MODE, SATP_MODE_SV32)
#define VA_BITS 32
#endif
#define IS_PGD(pte) (pte & SATP_MODE_CHOICE)

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

uintptr_t hpt_pmd_start = 0;
uintptr_t hpt_pte_start = 0;

int bitmap_and_hpt_init(uintptr_t bitmap_start, uint64_t bitmap_size,
			uintptr_t hpt_start, uint64_t hpt_size,
			uintptr_t hpt_pmd_start_, uintptr_t hpt_pte_start_)
{
	sbi_printf(
		"bitmap_and_hpt_init: bitmap_start: 0x%lx, bitmap_size: 0x%lx, hpt_start: 0x%lx, hpt_size: 0x%lx, hpt_pmd_start: 0x%lx, hpt_pte_start: 0x%lx\n",
		(uint64_t)bitmap_start, bitmap_size, (uint64_t)hpt_start,
		hpt_size, (uint64_t)hpt_pmd_start_, (uint64_t)hpt_pte_start_);

	int r;

	hpt_pmd_start = hpt_pmd_start_;
	hpt_pte_start = hpt_pte_start_;

	r = init_bitmap(bitmap_start, bitmap_size);
	if (r) {
		sbi_printf(
			"bitmap_and_hpt_init: bitmap init failed (error %d)\n",
			r);
		return r;
	}

	// TODO: check hpt mappings

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

int sm_set_shared(uintptr_t paddr_start, uint64_t size)
{
	// TODO: replace this with real implementation
	sbi_printf("sm_set_shared(0x%lx, 0x%lx) is called\n", paddr_start,
		   size);
	return 0;
}

/**
 * \brief Check whether it is a huge page table entry.
 * 
 * \param pte_addr The address of the pte entry.
 * \param pte_src The value of the pte entry.
 * \param page_num Return value. Huge page entry: 512, otherwise: not change.
 */
inline int check_huge_pt(uintptr_t pte_addr, uintptr_t pte_src, int *page_num)
{
	if (unlikely(((hpt_pmd_start) < pte_addr) &&
		     ((hpt_pte_start) > pte_addr))) {
		if ((pte_src & PTE_V) &&
		    ((pte_src & PTE_R) || (pte_src & PTE_W) ||
		     (pte_src & PTE_X))) {
			*page_num = 512;
		}
	}
	return 0;
}

/**
 * @brief Set the PTE
 *
 * @param addr physical address of the entry
 * @param pte the new value of the entry
 * @param page_num The number of pages to be set
 */
void set_single_pte(unsigned long *addr, unsigned long pte, size_t page_num)
{
	*((unsigned long *)addr) = pte;
}

/**
 * @brief Check if the action is valid, then perform it
 *
 * @param addr physical address of the entry
 * @param pte the new value of the entry
 * @param page_num The number of pages to be set
 */
void check_set_single_pte(unsigned long *addr, unsigned long pte,
			  size_t page_num)
{
	// TODO: check
	set_single_pte(addr, pte, page_num);
}

int sm_set_pte(unsigned long sub_fid, unsigned long *addr,
	       unsigned long pte_or_src, size_t size)
{
	// TODO: lock?
	int ret;
	switch (sub_fid) {
	case SBI_EXT_SM_SET_PTE_CLEAR:
		for (size_t i = 0; i < size / sizeof(uintptr_t); ++i, ++addr) {
			set_single_pte(addr, 0, 0);
		}
		ret = 0;
		break;
	case SBI_EXT_SM_SET_PTE_MEMCPY:
		// TODO: check
		if (size % 8) {
			ret = -1;
			sbi_printf(
				"sm_set_pte: SBI_EXT_SM_SET_PTE_MEMCPY: size align failed (addr: 0x%lx, src: 0x%lx, size: %ld)\n",
				(unsigned long)addr, pte_or_src, size);
		}
		int pte_num = 1;
		check_huge_pt((uintptr_t)addr, pte_or_src, &pte_num);
		size_t page_num = size >> 3;
		for (size_t i = 0; i < page_num; ++i, ++addr) {
			uintptr_t pte = *((uintptr_t *)pte_or_src + i);
			set_single_pte(addr, pte,
				       (!IS_PGD(pte) && (pte & PTE_V)) ? pte_num
								       : 0);
		}
		ret = 0;
		break;
	case SBI_EXT_SM_SET_PTE_SET_ONE:
		// TODO: check
		*addr = pte_or_src;
		ret   = 0;
		break;
	default:
		ret = -1;
		break;
	}
	return ret;
}
