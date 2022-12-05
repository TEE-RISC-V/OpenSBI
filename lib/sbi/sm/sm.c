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

int sm_prepare_cpu(uint64_t vm_id, uint64_t cpu_id) {
  // if (cpu_id != 0) {
  //   sbi_printf("sm_prepare_cpu(0x%lx, 0x%lx) is called\n", vm_id, cpu_id);
  // }

  return 0;
}