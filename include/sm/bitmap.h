#ifndef __BITMAP_H__
#define __BITMAP_H__

#include <sbi/sbi_types.h>
#include <sbi/riscv_locks.h>

#define DRAM_BASE 0x80000000 // TODO: get this value dynamically

/**
 * Init the bitmap, set the bitmap memory as the secure memory
 *
 * @param paddr_start The start address of the bitmap
 * @param bitmap_memory_size The bitmap memory size
 * @return 0 on success, error code on failure
 */
int init_bitmap(uintptr_t paddr_start, uint64_t bitmap_memory_size);

extern spinlock_t bitmap_lock;
#define lock_bitmap spin_lock(&bitmap_lock);
#define unlock_bitmap spin_unlock(&bitmap_lock);

/**
 * Check whether the pfn range contains the secure memory (not atomic)
 *
 * @param pfn_start The start page frame
 * @param num The number of pages in the pfn range
 * @return bool on success, negative error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
int contain_private_range(uint64_t pfn_start, uint64_t num);

/**
 * Check whether a range of physical memory is public or shared (not atomic).
 * This function assumes it is likely that the range is public or shared.
 * @param pfn_start The start page frame
 * @param num The number of pages in the pfn range
 * @return 1 on success, 0 when there exist private pages, negative error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
int test_public_shared_range(uint64_t pfn_start, uint64_t num);

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
int set_private_range(uint64_t pfn_start, uint64_t num);

/**
 * Similar to set_private_range (not atomic)
 *
 * @param pfn_start The start page frame
 * @param num The number of pages in the pfn range
 * @return 0 on success, error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
int set_public_range(uint64_t pfn_start, uint64_t num);

/**
 * Similar to set_private_range (not atomic)
 *
 * @param pfn_start The start page frame
 * @param num The number of pages in the pfn range
 * @return 0 on success, error code on failure
 * @note This function is not thread-safe, please use lock_bitmap while using it
 */
int set_shared_range(uint64_t pfn_start, uint64_t num);

#endif
