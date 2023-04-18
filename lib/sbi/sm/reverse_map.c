#include <sm/reverse_map.h>
#include <sm/bitmap.h>
#include <sm/sm.h>
#include <sbi/riscv_asm.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_string.h>

static bool reverse_map_initialized = false;

inline uint64_t pte_to_pfn(uintptr_t pte)
{
	return pte >> PTE_PPN_SHIFT;
}

inline bool pte_valid(uint64_t pte)
{
	return pte & PTE_V;
}

inline bool is_leaf_pte(uint64_t pte)
{
	return (pte & PTE_R) || (pte & PTE_W) || (pte & PTE_X);
}

#ifdef CONFIG_SBI_ECALL_SM_REVERSE_MAP
struct ReverseMap {
	uintptr_t *pte;
	struct ReverseMap *nxt;
};
struct ReverseMap **reverse_map, **reverse_map_end;
struct ReverseMap *reverse_map_empty_head;

#endif

int init_reverse_map(uintptr_t reverse_map_base, uint64_t reverse_map_size,
		     uint64_t dummy_head_size)
{
#ifdef CONFIG_SBI_ECALL_SM_REVERSE_MAP
	reverse_map = (struct ReverseMap **)reverse_map_base;
	sbi_memset((void *)reverse_map_base, 0, dummy_head_size);
	uintptr_t nodes_base   = reverse_map_base + dummy_head_size;
	reverse_map_end	       = (struct ReverseMap **)nodes_base;
	reverse_map_empty_head = (struct ReverseMap *)nodes_base;
	uintptr_t node_end     = reverse_map_base + reverse_map_size;
	uintptr_t cnt	       = 0;
	for (struct ReverseMap *node = (struct ReverseMap *)nodes_base;
	     node < (struct ReverseMap *)node_end; node++) {
		cnt++;
		node->pte = 0;
		node->nxt = (node + 1) < (struct ReverseMap *)node_end
				    ? (node + 1)
				    : NULL;
	}
#endif
	reverse_map_initialized = true;
	return 0;
}

extern uintptr_t hpt_start, hpt_pmd_start, hpt_pte_start, hpt_end;
int set_up_reverse_map_from_hpt_area()
{
#ifdef CONFIG_SBI_ECALL_SM_REVERSE_MAP
	// pgd -> 1GB(512 * 512 pages), pmd -> 2MB(512 pages), pte -> 4KB(1 page)
	for (uintptr_t *pgd = (uintptr_t *)hpt_start;
	     pgd < (uintptr_t *)hpt_pmd_start; pgd++) {
		if (pte_valid(*pgd) && is_leaf_pte(*pgd))
			if (add_reverse_map(*pgd, pgd, 512 * 512))
				return -1;
	}
	for (uintptr_t *pmd = (uintptr_t *)hpt_pmd_start;
	     pmd < (uintptr_t *)hpt_pte_start; pmd++) {
		if (pte_valid(*pmd) && is_leaf_pte(*pmd))
			if (add_reverse_map(*pmd, pmd, 512))
				return -1;
	}
	for (uintptr_t *pte = (uintptr_t *)hpt_pte_start;
	     pte < (uintptr_t *)hpt_end; pte++) {
		if (pte_valid(*pte) && is_leaf_pte(*pte))
			if (add_reverse_map(*pte, pte, 1))
				return -1;
	}
#endif
	return 0;
}

int add_reverse_map(uintptr_t pte, uintptr_t *pte_addr, uintptr_t page_num)
{
#ifdef CONFIG_SBI_ECALL_SM_REVERSE_MAP
	uintptr_t start = pte_to_pfn(pte) - ((uint64_t)DRAM_BASE >> PAGE_SHIFT);
	uintptr_t end	= start + page_num;
	for (uintptr_t i = start; i < end; i++) {
		if (reverse_map_empty_head == NULL) {
			sbi_printf("M mode: reverse_map_empty_head is NULL\n");
			return -1;
		}
		struct ReverseMap *cur = reverse_map_empty_head;
		reverse_map_empty_head = cur->nxt;
		cur->nxt	       = reverse_map[i];
		cur->pte	       = pte_addr;
		reverse_map[i]	       = cur;
	}
#endif
	return 0;
}

int delete_reverse_map(uintptr_t pte, uintptr_t *pte_addr, uintptr_t page_num)
{
#ifdef CONFIG_SBI_ECALL_SM_REVERSE_MAP
	uintptr_t start = pte_to_pfn(pte) - ((uint64_t)DRAM_BASE >> PAGE_SHIFT);
	uintptr_t end	= start + page_num;
	for (uintptr_t i = start; i < end; i++) {
		struct ReverseMap **prev = &reverse_map[i];
		struct ReverseMap *cur	 = reverse_map[i];
		while (cur != NULL) {
			if (cur->pte == pte_addr) {
				*prev		       = cur->nxt;
				cur->nxt	       = reverse_map_empty_head;
				reverse_map_empty_head = cur;
				break;
			} else {
				prev = &cur->nxt;
				cur  = cur->nxt;
			}
		}
	}
#endif
	return 0;
}

extern uint64_t bitmap_len;
int unmap_range(uint64_t pfn_start, uint64_t num)
{
	if (unlikely(!reverse_map_initialized)) {
		sbi_printf("M mode: %s : reverse map is not initialized\n",
			   __func__);
		return -1;
	}
	if (unlikely(pfn_start < ((uint64_t)DRAM_BASE >> PAGE_SHIFT))) {
		sbi_printf("M mode: %s : pfn_start is out of the DRAM range\n",
			   __func__);
		return -1;
	}
	pfn_start -= ((uint64_t)DRAM_BASE >> PAGE_SHIFT);
	if (unlikely(pfn_start + num > bitmap_len)) {
		sbi_printf("M mode: %s : meta is out of the bitmap range\n",
			   __func__);
		return -1;
	}

#ifdef CONFIG_SBI_ECALL_SM_REVERSE_MAP
	uint64_t idx_start = pfn_start - ((uint64_t)DRAM_BASE >> PAGE_SHIFT);
	uint64_t idx_end   = idx_start + num;
	struct ReverseMap *cur, *nxt;
	for (uintptr_t i = idx_start; i < idx_end; i++) {
		cur = NULL;
		nxt = reverse_map[i];
		while (nxt != NULL) {
			cur = nxt;
			// uintptr_t pfn = pte_to_pfn(*cur->pte);
			// int page_num = 1;
			// check_huge_pt((uintptr_t)cur->pte, *cur->pte, &page_num);
			// if(pfn + page_num > pfn_base && pfn < pfn_end)
			// {
			if (pte_valid(*cur->pte))
				*cur->pte ^= PTE_V;
			// } else {
			//   sbi_printf("M mode: unmap_mm_region: invalid pte\n");
			//   sbi_printf("  pfn 0x%lx, page_num 0x%x, pfn_base 0x%lx, pfn_end 0x%lx, is_leaf_pte %d\n", pfn, page_num, pfn_base, pfn_end, is_leaf_pte(*cur->pte));
			//   sbi_printf("  i 0x%lx, cur 0x%lx, pte address 0x%lx, pte value 0x%lx\n", i, (uintptr_t)cur, (uintptr_t)cur->pte, *cur->pte);
			//   return -1;
			// }
			nxt = cur->nxt;
		}
		if (cur) {
			cur->nxt	       = reverse_map_empty_head;
			reverse_map_empty_head = reverse_map[i];
			reverse_map[i]	       = NULL;
		}
	}
#else
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
#endif

	return 0;
}

int set_single_pte(uint64_t *addr, uint64_t pte, size_t page_num)
{
#ifdef CONFIG_SBI_ECALL_SM_REVERSE_MAP
	// delete the old reverse map
	uintptr_t old_pte_src = *addr;
	if (pte_valid(old_pte_src)) {
		int old_page_num = get_page_num((uintptr_t)addr);
		if (old_page_num < 0) {
			sbi_printf("M mode: set_single_pte: get_page_num failed\n");
			return -1;
		}
		if (delete_reverse_map(old_pte_src, addr, old_page_num) <
		    0) {
			sbi_printf("M mode: set_single_pte: delete_reverse_map failed\n");
			return -1;
		}
	}
#endif

	// update pte
	*((uint64_t *)addr) = pte;

#ifdef CONFIG_SBI_ECALL_SM_REVERSE_MAP
	// create the new reverse map
	if (page_num && is_leaf_pte(pte)) {
		if (add_reverse_map(pte, addr, page_num) < 0) {
			sbi_printf("M mode: set_single_pte: add_reverse_map failed\n");
			return -1;
		}
	}
#endif

	return 0;
}
