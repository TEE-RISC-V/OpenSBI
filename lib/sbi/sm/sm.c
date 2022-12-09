#include <sm/sm.h>
#include <sm/bitmap.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_unpriv.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/riscv_locks.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_pmp.h>
#include <sbi/sbi_math.h>

// TODO: more levels
#include <sbi/sbi_bitops.h>
#define MEGAPAGE_SIZE ((uintptr_t)(RISCV_PGSIZE << RISCV_PGLEVEL_BITS))
#if __riscv_xlen == 64
#define SATP_MODE_CHOICE INSERT_FIELD(0, SATP64_MODE, SATP_MODE_SV39)
#define VA_BITS 39
#define GIGAPAGE_SIZE (MEGAPAGE_SIZE << RISCV_PGLEVEL_BITS)
#else
#define SATP_MODE_CHOICE INSERT_FIELD(0, SATP32_MODE, SATP_MODE_SV32)
#define VA_BITS 32
#endif
#define IS_PGD(pte) (pte & SATP_MODE_CHOICE)

struct insn_match {
	unsigned long mask;
	unsigned long match;
};

#define INSN_MATCH_CSRRW	0x1073
#define INSN_MASK_CSRRW		0x707f
#define INSN_MATCH_CSRRS	0x2073
#define INSN_MASK_CSRRS		0x707f
#define INSN_MATCH_CSRRC	0x3073
#define INSN_MASK_CSRRC		0x707f
#define INSN_MATCH_CSRRWI	0x5073
#define INSN_MASK_CSRRWI	0x707f
#define INSN_MATCH_CSRRSI	0x6073
#define INSN_MASK_CSRRSI	0x707f
#define INSN_MATCH_CSRRCI	0x7073
#define INSN_MASK_CSRRCI	0x707f


static const struct insn_match csr_functions[] = {
	{
		.mask  = INSN_MASK_CSRRW,
		.match = INSN_MATCH_CSRRW,
	},
	{
		.mask  = INSN_MASK_CSRRS,
		.match = INSN_MATCH_CSRRS,
	},
	{
		.mask  = INSN_MASK_CSRRC,
		.match = INSN_MATCH_CSRRC,
	},
	{
		.mask  = INSN_MASK_CSRRWI,
		.match = INSN_MATCH_CSRRWI,
	},
	{
		.mask  = INSN_MASK_CSRRSI,
		.match = INSN_MATCH_CSRRSI,
	},
	{
		.mask  = INSN_MASK_CSRRCI,
		.match = INSN_MATCH_CSRRCI,
	},
	// {
	// 	.mask  = INSN_MASK_WFI,
	// 	.match = INSN_MATCH_WFI,
	// },
};


#define STORED_STATES 16
#define VM_BUCKETS 16

static struct vcpu_state states[VM_BUCKETS][STORED_STATES];
static spinlock_t global_lock = SPIN_LOCK_INITIALIZER;
unsigned int next_pmp_idx;
void update_min_usable_pmp_id(unsigned int pmp_idx)
{
	next_pmp_idx = pmp_idx;
}

void sm_init()
{
	// TODO: set up initial PMP registers

	sbi_printf("\nSM Init\n\n");

}

uintptr_t hpt_pmd_start = 0;
uintptr_t hpt_pte_start = 0;

int bitmap_and_hpt_init(uintptr_t bitmap_start, uint64_t bitmap_size,
			uintptr_t hpt_start, uint64_t hpt_size,
			uintptr_t hpt_pmd_start_, uintptr_t hpt_pte_start_)
{
	sbi_printf(
		"bitmap_and_hpt_init: bitmap_start: 0x%lx, bitmap_size: 0x%lx, hpt_start: 0x%lx, hpt_size: 0x%lx, hpt_pmd_start: 0x%lx, hpt_pte_start: 0x%lx\n",
		(uint64_t)bitmap_start, bitmap_size, (uint64_t)hpt_start,
		hpt_size, (uint64_t)hpt_pmd_start_, (uint64_t)hpt_pte_start_);

	int r;

	hpt_pmd_start = hpt_pmd_start_;
	hpt_pte_start = hpt_pte_start_;

	r = init_bitmap(bitmap_start, bitmap_size);
	if (r) {
		sbi_printf(
			"bitmap_and_hpt_init: bitmap init failed (error %d)\n",
			r);
		return r;
	}

	// TODO: check hpt mappings

	r = set_pmp_and_sync(next_pmp_idx++, 0, bitmap_start,
			     log2roundup(bitmap_size));
	if (r) {
		sbi_printf(
			"bitmap_and_hpt_init: PMP for bitmap init failed (error %d)\n",
			r);
		return r;
	}

	r = set_pmp_and_sync(next_pmp_idx++, 0, hpt_start,
			     log2roundup(hpt_size));
	if (r) {
		sbi_printf(
			"bitmap_and_hpt_init: PMP for HPT Area init failed (error %d)\n",
			r);
		return r;
	}

	sbi_printf("PMP set up for bitmap and HPT Area\n");

	return 0;
}

int sm_set_shared(uintptr_t paddr_start, uint64_t size)
{
	// TODO: replace this with real implementation
	sbi_printf("sm_set_shared(0x%lx, 0x%lx) is called\n", paddr_start,
		   size);
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

static inline void prepare_for_vm(struct sbi_trap_regs *regs, struct vcpu_state *state) {
	regs->mstatus &= ~MSTATUS_TSR;

	regs->extraInfo = 1;

	ulong deleg = csr_read(CSR_MIDELEG);
	csr_write(CSR_MIDELEG, deleg & ~(MIP_SSIP | MIP_STIP | MIP_SEIP));

  ulong exception = csr_read(CSR_MEDELEG);
  csr_write(CSR_MEDELEG, 0);

  state->prev_exception = exception;

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


struct vcpu_state* sm_prepare_cpu(uint64_t cpu_id) {
  if (cpu_id >= STORED_STATES) {
    return 0;
  }

  unsigned long vm_id = get_vm_id();

  struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

  scratch->vm_id = vm_id;
  scratch->cpu_id = cpu_id;

  return get_vcpu_state(vm_id, cpu_id);
}

int sm_create_cpu(uint64_t cpu_id, const struct sbi_trap_regs * regs) {
  if (cpu_id >= STORED_STATES) {
    return 1;
  }

  // TODO: keep track of creations so that hypervisor cannot abuse

  spin_lock(&global_lock);

  unsigned long vm_id = get_vm_id();

  struct vcpu_state *state = get_vcpu_state(vm_id, cpu_id);

  sbi_memcpy(&state->vcpu_state, regs, sizeof(struct sbi_trap_regs));
  state->vcpu_state.a1 = csr_read(CSR_STVAL);
  state->vcpu_state.a7 = csr_read(CSR_SCAUSE);
  state->trap.cause = -1LLU;

  SPIN_LOCK_INIT(state->lock);
  state->running = false;

  spin_unlock(&global_lock);

  return 0;
}


int sm_resume_cpu(uint64_t cpu_id, struct sbi_trap_regs * regs) {
  if (cpu_id >= STORED_STATES) {
    return 1;
  }

  regs->a1 = csr_read(CSR_STVAL);
  regs->a7 = csr_read(CSR_SCAUSE);

  struct vcpu_state *state = sm_prepare_cpu(cpu_id);

  spin_lock(&state->lock);

  if (state->running) {
    sbi_printf("CPU %ld is already running!", cpu_id);
    return 2;
  }

  state->running = true;

  ulong sepc = csr_read(CSR_SEPC);

  int ret = 0;

  switch (state->trap.cause) {
    case CAUSE_FETCH_ACCESS:
    case CAUSE_VIRTUAL_SUPERVISOR_ECALL:
    case CAUSE_FETCH_GUEST_PAGE_FAULT:
    case CAUSE_LOAD_GUEST_PAGE_FAULT:
    case CAUSE_STORE_GUEST_PAGE_FAULT: {
      prepare_for_vm(regs, state);
    }

    break;

    case CAUSE_VIRTUAL_INST_FAULT: {
      // Supervisor trying to return to next instruction

      if (sepc == state->vcpu_state.mepc + 4) {
        restore_registers(regs, state);
        prepare_for_vm(regs, state);
      } else if (sepc == csr_read(CSR_VSTVEC)) {
        restore_registers(regs, state);
        prepare_for_vm(regs, state);
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
      if (sepc == state->vcpu_state.mepc) {
        restore_registers(regs, state);
        prepare_for_vm(regs, state);
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
      prepare_for_vm(regs, state);
    }

    break;
  default:
    sbi_printf("I AM HERE %lu\n", state->trap.cause);

    sbi_hart_hang();
    break;
  }


trap_error:

  spin_unlock(&state->lock);

  return ret;
}

bool is_csr_fn(struct sbi_trap_info *trap) {
	ulong insn = trap->tval;

	bool is_csr = false;
	const struct insn_match *ifn;
	for (int i = 0; i < sizeof(csr_functions) / sizeof(struct insn_match); i++) {
		ifn = &csr_functions[i];
		if ((insn & ifn->mask) == ifn->match) {
			is_csr = true;
			break;
		}
	}

	return is_csr;
}

inline void hide_registers(struct sbi_trap_regs *regs, struct sbi_trap_info *trap, struct vcpu_state *state, bool is_virtual_insn_fault) {
	ulong insn = trap->tval;
	bool is_csr = is_virtual_insn_fault && is_csr_fn(trap);

	state->was_csr_insn = is_csr;
	ulong saved_value = 0;

	if (is_csr && is_virtual_insn_fault) saved_value = GET_RS1(insn, regs);

	ulong epc = regs->mepc;
	ulong status = regs->mstatus;
	ulong statusH = regs->mstatusH;

	sbi_memset(regs, 0, sizeof(struct sbi_trap_regs));

	regs->mepc = epc;
	regs->mstatus = status;
	regs->mstatusH = statusH;

	if (is_csr && is_virtual_insn_fault) *REG_PTR(insn, SH_RS1, regs) = saved_value;
}

int sm_preserve_cpu(struct sbi_trap_regs *regs, struct sbi_trap_info *trap) {
  struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

  int cpu_id = scratch->cpu_id;
  int vm_id = scratch->vm_id;

  if (cpu_id >= STORED_STATES) {
    return 1;
  }

  struct vcpu_state *state = get_vcpu_state(vm_id, cpu_id);

  spin_lock(&state->lock);

  if (!state->running) {
    sbi_printf("CPU %d is not running yet!", cpu_id);
    return 2;
  }

  sbi_memcpy(&state->vcpu_state, regs, sizeof(struct sbi_trap_regs));
	sbi_memcpy(&state->trap, trap, sizeof(struct sbi_trap_info));

  state->running = false;

  csr_write(CSR_MIDELEG, csr_read(CSR_MIDELEG) | MIP_SSIP | MIP_STIP | MIP_SEIP);
  csr_write(CSR_MEDELEG, state->prev_exception);

	switch (trap->cause) {
	case CAUSE_FETCH_ACCESS:
	case CAUSE_VIRTUAL_SUPERVISOR_ECALL:
	case CAUSE_FETCH_GUEST_PAGE_FAULT:
	case CAUSE_LOAD_GUEST_PAGE_FAULT:
	case CAUSE_STORE_GUEST_PAGE_FAULT:
		break;
	case CAUSE_VIRTUAL_INST_FAULT:
		hide_registers(regs, trap, state, true);
		break;
	default:
		hide_registers(regs, trap, state, false);
		break;
	}

  spin_unlock(&state->lock);

  return 0;
}
/**
 * \brief Check whether it is a huge page table entry.
 * 
 * \param pte_addr The address of the pte entry.
 * \param pte_src The value of the pte entry.
 * \param page_num Return value. Huge page entry: 512, otherwise: not change.
 */
inline int check_huge_pt(uintptr_t pte_addr, uintptr_t pte_src, int *page_num)
{
	if (unlikely(((hpt_pmd_start) < pte_addr) &&
		     ((hpt_pte_start) > pte_addr))) {
		if ((pte_src & PTE_V) &&
		    ((pte_src & PTE_R) || (pte_src & PTE_W) ||
		     (pte_src & PTE_X))) {
			*page_num = 512;
		}
	}
	return 0;
}

/**
 * @brief Set the PTE
 *
 * @param addr physical address of the entry
 * @param pte the new value of the entry
 * @param page_num The number of pages to be set
 */
void set_single_pte(unsigned long *addr, unsigned long pte, size_t page_num)
{
	*((unsigned long *)addr) = pte;
}

/**
 * @brief Check if the action is valid, then perform it
 *
 * @param addr physical address of the entry
 * @param pte the new value of the entry
 * @param page_num The number of pages to be set
 */
void check_set_single_pte(unsigned long *addr, unsigned long pte,
			  size_t page_num)
{
	// TODO: check
	set_single_pte(addr, pte, page_num);
}

int sm_set_pte(unsigned long sub_fid, unsigned long *addr,
	       unsigned long pte_or_src, size_t size)
{
	// TODO: lock?
	int ret;
	switch (sub_fid) {
	case SBI_EXT_SM_SET_PTE_CLEAR:
		for (size_t i = 0; i < size / sizeof(uintptr_t); ++i, ++addr) {
			set_single_pte(addr, 0, 0);
		}
		ret = 0;
		break;
	case SBI_EXT_SM_SET_PTE_MEMCPY:
		// TODO: check
		if (size % 8) {
			ret = -1;
			sbi_printf(
				"sm_set_pte: SBI_EXT_SM_SET_PTE_MEMCPY: size align failed (addr: 0x%lx, src: 0x%lx, size: %ld)\n",
				(unsigned long)addr, pte_or_src, size);
		}
		int pte_num = 1;
		check_huge_pt((uintptr_t)addr, pte_or_src, &pte_num);
		size_t page_num = size >> 3;
		for (size_t i = 0; i < page_num; ++i, ++addr) {
			uintptr_t pte = *((uintptr_t *)pte_or_src + i);
			set_single_pte(addr, pte,
				       (!IS_PGD(pte) && (pte & PTE_V)) ? pte_num
								       : 0);
		}
		ret = 0;
		break;
	case SBI_EXT_SM_SET_PTE_SET_ONE:
		// TODO: check
		*addr = pte_or_src;
		ret   = 0;
		break;
	default:
		ret = -1;
		break;
	}
	return ret;
}
