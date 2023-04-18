#ifndef __REVERSE_MAP_H__
#define __REVERSE_MAP_H__

#include <sbi/sbi_types.h>

/**
 * Init the reverse map, set the reverse map memory as the secure memory
 *
 * @param reverse_map_base The start address of the reverse map
 * @param reverse_map_size The reverse map memory size.
 * @param dummy_head_size The dummy head memory size.
 * @return 0 on success, error code on failure
 */
int init_reverse_map(uintptr_t reverse_map_base, uint64_t reverse_map_size,
		     uint64_t dummy_head_size);

/**
 * Set up a reverse map from HPT Area
 * @return 0 on success, negative error code on failure
*/
int set_up_reverse_map_from_hpt_area();

/**
 * @brief Add a reverse map
 *
 * @param pte The PTE entry
 * @param pte_addr The address of the PTE
 * @param page_num The number of pages the PTE covers
 * @return 0 on success, negative error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
int add_reverse_map(uintptr_t pte, uintptr_t *pte_addr, uintptr_t page_num);

/**
 * @brief Delete a reverse map
 *
 * @param pte The old PTE entry
 * @param pte_addr The address of the PTE
 * @param page_num The number of pages the PTE covers
 * @return 0 on success, negative error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
int delete_reverse_map(uintptr_t pte, uintptr_t *pte_addr, uintptr_t page_num);

/**
 * Invalidate all PTEs in HPT Area that point to the range of physical pages (not atomic).
 * @param pfn_start The start page frame
 * @param num The number of pages in the pfn range
 * @return 0 on success, negative error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
int unmap_range(uint64_t pfn_start, uint64_t num);

/**
 * @brief Set the PTE
 *
 * @param addr physical address of the entry
 * @param pte the new value of the entry
 * @param page_num The number of pages to be set
 * @return 0 on success, negative error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
int set_single_pte(uint64_t *addr, uint64_t pte, size_t page_num);

#endif
