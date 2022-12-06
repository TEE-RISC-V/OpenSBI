#include <sm/sm.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_scratch.h>

#define STORED_STATES 16
static struct vcpu_state states[STORED_STATES];


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
  if (cpu_id >= STORED_STATES) {
    return 1;
  }

  struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

  sbi_memcpy(&scratch->state, &states[cpu_id], sizeof(struct vcpu_state));
  return 0;
}

int sm_preserve_cpu(uint64_t vm_id, uint64_t cpu_id) {
  // if (cpu_id != 0) {
  //   sbi_printf("sm_prepare_cpu(0x%lx, 0x%lx) is called\n", vm_id, cpu_id);
  // }

    if (cpu_id >= STORED_STATES) {
    return 1;
  }

  struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

  sbi_memcpy(&states[cpu_id], &scratch->state, sizeof(struct vcpu_state));


  return 0;
}