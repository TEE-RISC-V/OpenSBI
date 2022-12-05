#include <sm/sm.h>
#include <sbi/sbi_console.h>

int sm_init()
{
	// TODO: set up initial PMP registers

	sbi_printf("\nSM Init\n\n");

  return 0;
}

int sm_set_shared(uint64_t paddr_start, uint64_t size) {
  // TODO: replace this with real implementation
  sbi_printf("sm_set_shared(0x%lx, 0x%lx) is called\n", paddr_start, size);
  return 0;
}
