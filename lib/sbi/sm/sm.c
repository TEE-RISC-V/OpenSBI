#include <sm/sm.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>
// #include <sbi/sbi_hart.h>
#include <sbi/sbi_unpriv.h>

#define STORED_STATES 16
#define VM_BUCKETS 16

static struct vcpu_state states[VM_BUCKETS][STORED_STATES];


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

uint64_t get_vm_id() {
  unsigned long hgatp = csr_read(CSR_HGATP);

  return (hgatp | HGATP64_VMID_MASK) >> HGATP_VMID_SHIFT;
}

struct vcpu_state* get_vcpu_state(unsigned long vm_id, uint64_t cpu_id) {
  // TODO: make this handle "conflicts" properly
  return &states[vm_id % VM_BUCKETS][cpu_id];
}

int sm_prepare_cpu(uint64_t cpu_id) {
  // TODO: make this thread safe
  
  // sbi_printf("HELLO2 %lu %lu\n", vm_id, cpu_id);
  if (cpu_id >= STORED_STATES) {
    return 1;
  }

  unsigned long vm_id = get_vm_id();

  struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

  if (scratch->storing_vcpu != 0) {
    sbi_printf("sm error1 %u %lu %lu %lu %lu\n", scratch->storing_vcpu, scratch->vm_id, scratch->cpu_id, vm_id, cpu_id);
  }

  scratch->storing_vcpu = 1;
  scratch->vm_id = vm_id;
  scratch->cpu_id = cpu_id;

  sbi_memcpy(&scratch->state, get_vcpu_state(vm_id, cpu_id), sizeof(struct vcpu_state));
  return 0;
}

int sm_create_cpu(uint64_t cpu_id, const struct sbi_trap_regs * regs) {
  if (cpu_id >= STORED_STATES) {
    return 1;
  }

  unsigned long vm_id = get_vm_id();

  struct vcpu_state *state = get_vcpu_state(vm_id, cpu_id);

  sbi_memcpy(&state->vcpu_state, regs, sizeof(struct sbi_trap_regs));
  state->vcpu_state.a1 = csr_read(CSR_STVAL);
  state->vcpu_state.a7 = csr_read(CSR_SCAUSE);
  state->trap.cause = -1LLU;

  return 0;
}


int sm_resume_cpu(uint64_t cpu_id, const struct sbi_trap_regs * regs) {
  if (cpu_id >= STORED_STATES) {
    return 1;
  }

  unsigned long vm_id = get_vm_id();

  get_vcpu_state(vm_id, cpu_id)->trap.cause = -1LLU;

  return 0;
}

int sm_preserve_cpu(uint64_t cpu_id) {
  // if (cpu_id != 0) {
  //   sbi_printf("sm_prepare_cpu(0x%lx, 0x%lx) is called\n", vm_id, cpu_id);
  // }

  if (cpu_id >= STORED_STATES) {
    return 1;
  }

  struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

  if (scratch->storing_vcpu != 1) {
    sbi_printf("sm error2 %lu %d\n", ~(1UL << (__riscv_xlen - 1)) & csr_read(CSR_SCAUSE), scratch->storing_vcpu);
  }

  scratch->storing_vcpu = 0;

  unsigned long vm_id = get_vm_id();

  sbi_memcpy(get_vcpu_state(vm_id, cpu_id), &scratch->state, sizeof(struct vcpu_state));


  return 0;
}