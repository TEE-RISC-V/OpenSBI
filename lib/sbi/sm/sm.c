#include <sm/sm.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_unpriv.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>

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

static inline void prepare_for_vm(struct sbi_trap_regs *regs, struct sbi_scratch *scratch) {
	regs->mstatus &= ~MSTATUS_TSR;
	scratch->storing_vcpu = 0;

	regs->extraInfo = 1;

	ulong deleg = csr_read(CSR_MIDELEG);
	csr_write(CSR_MIDELEG, deleg & ~(MIP_SSIP | MIP_STIP | MIP_SEIP));

	return;
}


static inline void restore_registers(struct sbi_trap_regs *regs, struct vcpu_state *state) {
	ulong orig_insn = state->trap.tval;
	ulong reg_value = 0;

	if (state->was_csr_insn) {
		reg_value = *REG_PTR(orig_insn, SH_RD, regs);
	}

	ulong epc = regs->mepc;
	ulong status = regs->mstatus;
	ulong statusH = regs->mstatusH;

	sbi_memcpy(regs, &state->vcpu_state, sizeof(struct sbi_trap_regs));

	regs->mepc = epc;
	regs->mstatus = status;
	regs->mstatusH = statusH;

	if (state->was_csr_insn) {
		SET_RD(orig_insn, regs, reg_value);
	}

	return;
}


int sm_prepare_cpu(uint64_t cpu_id) {
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


int sm_resume_cpu(uint64_t cpu_id, struct sbi_trap_regs * regs) {
  if (cpu_id >= STORED_STATES) {
    return 1;
  }

  regs->a1 = csr_read(CSR_STVAL);
  regs->a7 = csr_read(CSR_SCAUSE);

  int ret = sm_prepare_cpu(cpu_id);

  ulong sepc = csr_read(CSR_SEPC);
  struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

  struct vcpu_state *state = &scratch->state;


  switch (state->trap.cause) {
    case CAUSE_FETCH_ACCESS:
    case CAUSE_VIRTUAL_SUPERVISOR_ECALL:
    case CAUSE_FETCH_GUEST_PAGE_FAULT:
    case CAUSE_LOAD_GUEST_PAGE_FAULT:
    case CAUSE_STORE_GUEST_PAGE_FAULT: {
      prepare_for_vm(regs, scratch);
    }

    break;

    case CAUSE_VIRTUAL_INST_FAULT: {
      // ulong sepc = csr_read(CSR_SEPC);
      // Supervisor trying to return to next instruction

      if (sepc == state->vcpu_state.mepc + 4) {
        restore_registers(regs, state);
        prepare_for_vm(regs, scratch);
      } else if (sepc == csr_read(CSR_VSTVEC)) {
        restore_registers(regs, state);
        prepare_for_vm(regs, scratch);
      } else {
        sbi_printf("BRUH 0x%" PRILX "0x%" PRILX "\n", sepc, state->vcpu_state.mepc);
        ret = SBI_EUNKNOWN;

        goto trap_error;
      }
    }
    break;
    
    case IRQ_S_SOFT_FLIPPED:
    case IRQ_S_TIMER_FLIPPED:
    case IRQ_S_EXT_FLIPPED:
    case IRQ_S_GEXT_FLIPPED: {
      // TODO 1: 
      if (sepc == state->vcpu_state.mepc) {
        restore_registers(regs, state);
        prepare_for_vm(regs, scratch);
      } else {
        sbi_printf("BRUH2 0x%" PRILX "0x%" PRILX "\n", sepc, state->vcpu_state.mepc);
        ret = SBI_EUNKNOWN;

        goto trap_error;
      }

    }

    break;

    // Special case when running for the first time
    case -1LLU: {
      restore_registers(regs, state);
      prepare_for_vm(regs, scratch);

      sbi_printf("hello there!\n");
    }

    break;
  default:
    sbi_printf("I AM HERE %lu\n", state->trap.cause);

    // sbi_hart_hang();
    break;
  }


trap_error:



  return ret;
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