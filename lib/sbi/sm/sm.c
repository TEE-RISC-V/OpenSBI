#include <sm/sm.h>
#include <sm/bitmap.h>
#include <sbi/sbi_console.h>

void sm_init()
{
	// TODO: set up initial PMP registers

	sbi_printf("\nSM Init\n\n");
}

int bitmap_and_hpt_init(uintptr_t bitmap_start, uint64_t bitmap_size,
			uintptr_t hpt_start, uint64_t hpt_size)
{
	sbi_printf(
		"bitmap_and_hpt_init: bitmap_start: 0x%lx, bitmap_size: 0x%lx, hpt_start: 0x%lx, hpt_size: 0x%lx\n",
		(uint64_t)bitmap_start, bitmap_size, (uint64_t)hpt_start,
		hpt_size);

	int r;

	r = init_bitmap(bitmap_start, bitmap_size);
	if (r)
		return r;
	// TODO: init hpt and check mappings
	// TODO: set up PMP registers

	return 0;
}

int sm_set_shared(uintptr_t paddr_start, uint64_t size)
{
	// TODO: replace this with real implementation
	sbi_printf("sm_set_shared(0x%lx, 0x%lx) is called\n", paddr_start,
		   size);
	return 0;
}
