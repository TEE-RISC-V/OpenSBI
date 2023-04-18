#include <sm/reverse_map.h>
#include <sm/bitmap.h>
#include <sbi/riscv_asm.h>
#include <sbi/sbi_console.h>

static bool reverse_map_initialized = false;

inline uint64_t pte_to_pfn(uintptr_t pte)
{
	return pte >> PTE_PPN_SHIFT;
}

int init_reverse_map(uintptr_t reverse_map_base, uint64_t reverse_map_size, uint64_t dummy_head_size) {
        reverse_map_initialized = true;
        return 0;
}

extern uint64_t bitmap_len;
extern uintptr_t hpt_pmd_start, hpt_pte_start, hpt_end;
int unmap_range(uint64_t pfn_start, uint64_t num)
{
	if (unlikely(!reverse_map_initialized)) {
		sbi_printf("M mode: %s : reverse map is not initialized\n",
			   __func__);
		return -1;
	}
	if (unlikely(pfn_start < ((uint64_t)DRAM_BASE >> PAGE_SHIFT))) {
		sbi_printf(
			"M mode: %s : pfn_start is out of the DRAM range\n",
			__func__);
		return -1;
	}
	pfn_start -= ((uint64_t)DRAM_BASE >> PAGE_SHIFT);
	if (unlikely(pfn_start + num > bitmap_len)) {
		sbi_printf("M mode: %s : meta is out of the bitmap range\n",
			   __func__);
		return -1;
	}

	uint64_t pfn_end = pfn_start + num;

	// unmap PMD
	for (uint64_t *pte = (uint64_t *)hpt_pmd_start;
	     pte < (uint64_t *)hpt_pte_start; pte++) {
		if (!(*pte & PTE_V))
			continue;
		uint64_t pfn = pte_to_pfn(*pte);
		if ((*pte & PTE_R) || (*pte & PTE_W) ||
		    (*pte & PTE_X)) { // is leaf
			if (!(pfn_end < pfn ||
			      pfn + 512 < pfn_start)) { // overlap
				*pte = *pte ^ PTE_V;
			}
		}
	}

	// unmap PTE
	for (uint64_t *pte = (uint64_t *)hpt_pte_start;
	     pte < (uint64_t *)hpt_end; pte++) {
		if (!(*pte & PTE_V))
			continue;
		uint64_t pfn = pte_to_pfn(*pte);
		if (pfn_start <= pfn && pfn < pfn_end) { // overlap
			*pte = *pte ^ PTE_V;
		}
	}

	return 0;
}

int set_single_pte(uint64_t *addr, uint64_t pte, size_t page_num)
{
	*((uint64_t *)addr) = pte;
	return 0;
}
