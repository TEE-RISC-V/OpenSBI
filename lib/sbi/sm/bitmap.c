#include <sm/bitmap.h>
#include <sbi/riscv_asm.h>
#include <sbi/sbi_console.h>

typedef u8 page_meta_t;
#define PUBLIC_PAGE ((page_meta_t)0xFF)
#define PRIVATE_PAGE ((page_meta_t)0x00)
#define SHARED_PAGE ((page_meta_t)0x0F)
#define IS_PUBLIC_PAGE(meta) (meta == PUBLIC_PAGE)
#define IS_PRIVATE_PAGE(meta) (!meta)
#define IS_SHARED_PAGE(meta) (meta == SHARED_PAGE)
#define IS_PUBLIC_OR_SHARED_PAGE(meta) (!!meta)

#define DRAM_BASE 0x80000000 // TODO: get this value dynamically

spinlock_t bitmap_lock = SPIN_LOCK_INITIALIZER;

static bool bitmap_initialized = false;
static page_meta_t *bitmap;
static uint64_t bitmap_len;

inline uint64_t pte_to_pfn(uintptr_t pte)
{
	return pte >> PTE_PPN_SHIFT;
}

int init_bitmap(uintptr_t paddr_start, uint64_t bitmap_memory_size)
{
	bitmap_initialized = true;
	bitmap		   = (page_meta_t *)paddr_start;
	bitmap_len	   = bitmap_memory_size / sizeof(page_meta_t);

	page_meta_t *meta = bitmap;
	uintptr_t cur	  = 0;
	while (cur < bitmap_memory_size) {
		*meta = PUBLIC_PAGE;
		meta += 1;
		cur += sizeof(page_meta_t);
	}

	return 0;
}

#define check_input_and_update_pfn_start(pfn_start, num)                     \
	if (unlikely(!bitmap_initialized)) {                                 \
		sbi_printf("M mode: %s : bitmap is not initialized\n",       \
			   __func__);                                        \
		return -1;                                                   \
	}                                                                    \
	if (unlikely(pfn_start < ((uint64_t)DRAM_BASE >> PAGE_SHIFT))) {     \
		sbi_printf(                                                  \
			"M mode: %s : pfn_start is out of the DRAM range\n", \
			__func__);                                           \
		return -1;                                                   \
	}                                                                    \
	pfn_start -= ((uint64_t)DRAM_BASE >> PAGE_SHIFT);                    \
	if (unlikely(pfn_start + num > bitmap_len)) {                        \
		sbi_printf("M mode: %s : meta is out of the bitmap range\n", \
			   __func__);                                        \
		return -1;                                                   \
	}

int contain_private_range(uint64_t pfn_start, uint64_t num)
{
	check_input_and_update_pfn_start(pfn_start, num);

	page_meta_t *meta = &bitmap[pfn_start];
	uintptr_t cur	  = 0;
	while (cur < num) {
		if (IS_PRIVATE_PAGE(*meta))
			return 1;
		meta += 1;
		cur += 1;
	}

	return 0;
}

int test_public_shared_range(uintptr_t pfn_start, uintptr_t num)
{
	check_input_and_update_pfn_start(pfn_start, num);

	page_meta_t *meta = &bitmap[pfn_start];
	uintptr_t cur	  = 0;
	while (cur < num) {
		if (!IS_PUBLIC_OR_SHARED_PAGE(*meta))
			return 0;
		meta += 1;
		cur += 1;
	}

	return 1;
}

extern uintptr_t hpt_pmd_start, hpt_pte_start, hpt_end;
int unmap_range(uint64_t pfn_start, uint64_t num)
{
	check_input_and_update_pfn_start(pfn_start, num);

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

int set_private_range(uint64_t pfn_start, uint64_t num)
{
	check_input_and_update_pfn_start(pfn_start, num);

	page_meta_t *meta = &bitmap[pfn_start];
	uintptr_t cur	  = 0;
	while (cur < num) {
		*meta = PRIVATE_PAGE;
		meta += 1;
		cur += 1;
	}

	return 0;
}

int set_public_range(uint64_t pfn_start, uint64_t num)
{
	check_input_and_update_pfn_start(pfn_start, num);

	page_meta_t *meta = &bitmap[pfn_start];
	uintptr_t cur	  = 0;
	while (cur < num) {
		*meta = PUBLIC_PAGE;
		meta += 1;
		cur += 1;
	}

	return 0;
}

int set_shared_range(uint64_t pfn_start, uint64_t num)
{
	check_input_and_update_pfn_start(pfn_start, num);

	page_meta_t *meta = &bitmap[pfn_start];
	uintptr_t cur	  = 0;
	while (cur < num) {
		*meta = SHARED_PAGE;
		meta += 1;
		cur += 1;
	}

	return 0;
}
