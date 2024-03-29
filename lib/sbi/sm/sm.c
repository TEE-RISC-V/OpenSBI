#include <sm/sm.h>
#include <sm/bitmap.h>
#include <sm/reverse_map.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_unpriv.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/riscv_locks.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_pmp.h>
#include <sbi/sbi_tvm.h>
#include <sbi/sbi_math.h>

// TODO: more levels
#include <sbi/sbi_bitops.h>
#if __riscv_xlen == 64
#define SATP_MODE_CHOICE INSERT_FIELD(0, SATP64_MODE, SATP_MODE_SV39)
#else
#define SATP_MODE_CHOICE INSERT_FIELD(0, SATP32_MODE, SATP_MODE_SV32)
#endif
#define IS_PGD(pte) (pte & SATP_MODE_CHOICE)

struct insn_match {
	unsigned long mask;
	unsigned long match;
};

#define INSN_MATCH_CSRRW 0x1073
#define INSN_MASK_CSRRW 0x707f
#define INSN_MATCH_CSRRS 0x2073
#define INSN_MASK_CSRRS 0x707f
#define INSN_MATCH_CSRRC 0x3073
#define INSN_MASK_CSRRC 0x707f
#define INSN_MATCH_CSRRWI 0x5073
#define INSN_MASK_CSRRWI 0x707f
#define INSN_MATCH_CSRRSI 0x6073
#define INSN_MASK_CSRRSI 0x707f
#define INSN_MATCH_CSRRCI 0x7073
#define INSN_MASK_CSRRCI 0x707f

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
inline uintptr_t pte_to_ppn(uintptr_t pte)
{
	return pte >> PTE_PPN_SHIFT;
}
inline uintptr_t pte_to_phys(uintptr_t pte)
{
	return pte_to_ppn(pte) << PAGE_SHIFT;
}

unsigned int next_pmp_idx;
void update_min_usable_pmp_id(unsigned int pmp_idx)
{
	next_pmp_idx = pmp_idx;
}

void sm_init()
{
	if (set_pmp_and_sync(
		    next_pmp_idx++, 0, 0x80000000,
		    log2roundup(0x200000))) { // TODO: check the size of SM
		sbi_panic("Unable to use PMP to protect SM\n");
	}
	sbi_printf("\nSM Init\n\n");
}

bool check_enabled  = false;
uintptr_t hpt_start = 0, hpt_end = 0;
uintptr_t hpt_pmd_start = 0;
uintptr_t hpt_pte_start = 0;

int bitmap_and_hpt_init(uintptr_t bitmap_start, uint64_t bitmap_size,
			uintptr_t hpt_start_, uint64_t hpt_size,
			uintptr_t hpt_pmd_start_, uintptr_t hpt_pte_start_)
{
	sbi_printf(
		"bitmap_and_hpt_init: bitmap_start: 0x%lx, bitmap_size: 0x%lx, hpt_start: 0x%lx, hpt_size: 0x%lx, hpt_pmd_start: 0x%lx, hpt_pte_start: 0x%lx\n",
		(uint64_t)bitmap_start, bitmap_size, (uint64_t)hpt_start_,
		hpt_size, (uint64_t)hpt_pmd_start_, (uint64_t)hpt_pte_start_);

	int r;

	hpt_start     = hpt_start_;
	hpt_end	      = hpt_start + hpt_size;
	hpt_pmd_start = hpt_pmd_start_;
	hpt_pte_start = hpt_pte_start_;

	r = init_bitmap(bitmap_start, bitmap_size);
	if (r) {
		sbi_printf(
			"bitmap_and_hpt_init: bitmap init failed (error %d)\n",
			r);
		return r;
	}

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

int sm_reverse_map_init(uintptr_t reverse_map_start, uint64_t reverse_map_size)
{
	sbi_printf(
		"sm_reverse_map_init: reverse_map_start: 0x%lx, reverse_map_size: 0x%lx\n",
		(uint64_t)reverse_map_start, reverse_map_size);

	int r = init_reverse_map(reverse_map_start, reverse_map_size,
				 hpt_end - hpt_start);
	if (r) {
		sbi_printf(
			"sm_reverse_map_init: reverse map init failed (error %d)\n",
			r);
		return r;
	}

#ifdef CONFIG_SBI_ECALL_SM_REVERSE_MAP
	r = set_pmp_and_sync(next_pmp_idx++, 0, reverse_map_start,
			     log2roundup(reverse_map_size));
	if (r) {
		sbi_printf(
			"bitmap_and_hpt_init: PMP for reverse map init failed (error %d)\n",
			r);
		return r;
	}
#endif

	return 0;
}

int monitor_init(uintptr_t *mstatus)
{
	// ensure PGD entries only map to HPT PMD Area
	for (uintptr_t *pte = (uintptr_t *)hpt_start;
	     pte < (uintptr_t *)hpt_pmd_start; pte++) {
		if (!IS_PGD(*pte) && (*pte & PTE_V)) {
			uintptr_t nxt_pt = pte_to_phys(*pte);

			if ((nxt_pt < hpt_pmd_start) ||
			    (nxt_pt >= hpt_pte_start)) {
				sbi_printf(
					"[%s] Invalid PGD entry(0x%lx): 0x%lx (a mapping to address 0x%lx), should be in [0x%lx, 0x%lx)\n",
					__func__, (uintptr_t)pte, *pte, nxt_pt,
					hpt_pmd_start, hpt_pte_start);
				return -1;
			}
		}
	}

	// check non-leaf PMD entries only map to HPT PTE Area
	for (uintptr_t *pte = (uintptr_t *)hpt_pmd_start;
	     pte < (uintptr_t *)hpt_pte_start; pte++) {
		if ((*pte & PTE_V) && !(*pte & PTE_R) && !(*pte & PTE_W) &&
		    !(*pte & PTE_X)) {
			uintptr_t nxt_pt = pte_to_phys(*pte);
			if ((nxt_pt < hpt_pte_start) || (nxt_pt >= hpt_end)) {
				sbi_printf(
					"[%s] Invalid PMD entry(0x%lx): 0x%lx (a mapping to address 0x%lx), should be in [0x%lx, 0x%lx)\n",
					__func__, (uintptr_t)pte, *pte, nxt_pt,
					hpt_pte_start, hpt_end);
				return -1;
			}
		}
	}

	set_tvm_and_sync();
	*mstatus = csr_read(CSR_MSTATUS);

	if (set_up_reverse_map_from_hpt_area() < 0) {
		sbi_printf(
			"monitor_init: set_up_reverse_map_from_hpt_area failed\n");
		return -1;
	}

	check_enabled = true;
	sbi_printf("\nSM Monitor Init\n\n");
	return 0;
}

/**
 * @brief translate guest physical address to host physical address by HGATP
 *
 * @param gpa guest physical address
 * @param pte_size the size of memory that the leaf PTE maps
 * @return host physical address
 */
inline static uintptr_t gpa_to_hpa(uintptr_t gpa, uintptr_t *size)
{
	const uintptr_t hgatp = csr_read(CSR_HGATP);
#if __riscv_xlen == 32
	const uintptr_t hgatp_mode = hgatp >> HGATP32_MODE_SHIFT;
	uintptr_t page_table_ppn   = hgatp & HGATP32_PPN;
#else
	const uintptr_t hgatp_mode = hgatp >> HGATP64_MODE_SHIFT;
	uintptr_t page_table_ppn   = hgatp & HGATP64_PPN;
#endif
	unsigned ppn_num, pte_size, vpn_len;
	if (unlikely(hgatp_mode == 0)) { // Bare
		ppn_num = 0;
	} else if (unlikely(hgatp_mode == 1)) { // Sv32x4
		pte_size = 4;
		ppn_num	 = 2;
		vpn_len	 = 10;
	} else if (8 <= hgatp_mode &&
		   hgatp_mode <= 10) { // Sv39x4, Sv48x4, Sv57x4
		// WARNING: Sv57x4's GPA formats are inconsistent in riscv-privileged-20211203
		pte_size = 8;
		ppn_num	 = hgatp_mode - 8 + 3;
		vpn_len	 = 9;
	} else {
		sbi_printf("gpa_to_hpa: Unsupported HGATP mode: %ld\n",
			   hgatp_mode);
		return 0;
	}
	for (int i = ppn_num - 1; i >= 0; i--) {
		uintptr_t pte;
		uintptr_t vpn = gpa >> (i * vpn_len + 12);
		if (i != ppn_num - 1)
			vpn &= (1 << vpn_len) - 1;
		uintptr_t offset = pte_size * vpn;
		if (unlikely(hgatp_mode == 1)) {
			uint32_t tmp =
				*(uint32_t *)(page_table_ppn * PAGE_SIZE +
					      offset);
			pte = tmp;
		} else {
			uint64_t tmp =
				*(uint64_t *)(page_table_ppn * PAGE_SIZE +
					      offset);
			pte = tmp;
		}
		if (unlikely(!(pte & PTE_V))) {
			sbi_printf("gpa_to_hpa: Invalid PTE: 0x%lx\n", pte);
			return 0;
		}
		if (pte & PTE_R || pte & PTE_W || pte & PTE_X) {
			*size = 1 << (i * vpn_len + 12);
			return pte_to_phys(pte) + (gpa & (*size - 1));
		} else {
			page_table_ppn = pte_to_ppn(pte);
		}
	}
	sbi_printf("gpa_to_hpa: levels more than expected: 0xgpa %lx\n", gpa);
	return 0;
}

int sm_set_bounce_buffer(uintptr_t gpaddr_start, uint64_t size)
{
	sbi_printf(
		"SM is trying to set bounce buffer as shared memory(gpa=0x%lx, size=0x%lx)\n",
		gpaddr_start, size);
	uintptr_t mapping_size;
	lock_bitmap;
	while (size) {
		uintptr_t hpaddr_start =
			gpa_to_hpa(gpaddr_start, &mapping_size);
		if (hpaddr_start == 0) {
			sbi_printf("sm_set_bounce_buffer: gpa_to_hpa failed\n");
			return -1;
		}
		int ret = set_shared_range(hpaddr_start >> PAGE_SHIFT,
					   mapping_size >> PAGE_SHIFT);
		if (unlikely(ret))
			sbi_printf(
				"sm_set_shared(gpa: 0x%lx, hpa: 0x%lx, 0x%lx) errno: %d\n",
				gpaddr_start, hpaddr_start, mapping_size, ret);
		gpaddr_start += mapping_size;
		size -= mapping_size;
	}
	unlock_bitmap;
	sbi_printf("sm_set_bounce_buffer finished successfully\n");
	return 0;
}

uint64_t get_vm_id()
{
	unsigned long hgatp = csr_read(CSR_HGATP);

	return (hgatp & HGATP64_VMID_MASK) >> HGATP_VMID_SHIFT;
}

static void mask_hgatp(struct vcpu_state *state) {
	state->hgatp = csr_read(CSR_HGATP);
	csr_write(CSR_HGATP, state->hgatp & HGATP64_VMID_MASK);
}

static void restore_hgatp(struct vcpu_state *state) {
	csr_write(CSR_HGATP, state->hgatp);
	state->hgatp = 0;
}

struct vcpu_state *get_vcpu_state(unsigned long vm_id, uint64_t cpu_id)
{
	// TODO: make this handle "conflicts" properly
	return &states[vm_id % VM_BUCKETS][cpu_id];
}

static inline void prepare_for_vm(struct sbi_trap_regs *regs,
				  struct vcpu_state *state)
{
	regs->mstatus &= ~MSTATUS_TSR;

	regs->extraInfo = 1;

	ulong deleg = csr_read(CSR_MIDELEG);
	csr_write(CSR_MIDELEG, deleg & ~(MIP_SSIP | MIP_STIP | MIP_SEIP));

	ulong exception = csr_read(CSR_MEDELEG);
	csr_write(CSR_MEDELEG, 0);

	state->prev_exception = exception;

	restore_hgatp(state);

	return;
}

static inline void restore_registers(struct sbi_trap_regs *regs,
				     struct vcpu_state *state)
{
	ulong orig_insn = state->trap.tval;
	ulong reg_value = 0;

	if (state->was_csr_insn) {
		reg_value = *REG_PTR(orig_insn, SH_RD, regs);
	}

	ulong epc     = regs->mepc;
	ulong status  = regs->mstatus;
	ulong statusH = regs->mstatusH;

	sbi_memcpy(regs, &state->vcpu_state, sizeof(struct sbi_trap_regs));

	regs->mepc     = epc;
	regs->mstatus  = status;
	regs->mstatusH = statusH;

	if (state->was_csr_insn) {
		SET_RD(orig_insn, regs, reg_value);
	}

	return;
}

inline void restore_registers_ecall(struct sbi_trap_regs *regs, struct vcpu_state *state)
{
	ulong epc     = regs->mepc;
	ulong status  = regs->mstatus;
	ulong statusH = regs->mstatusH;
	ulong a0 = regs->a0;
	ulong a1 = regs->a1;
	ulong a2 = regs->a2;
	ulong a3 = regs->a3;
	ulong a4 = regs->a4;
	ulong a5 = regs->a5;
	ulong a6 = regs->a6;
	ulong a7 = regs->a7;

	sbi_memcpy(regs, &state->vcpu_state, sizeof(struct sbi_trap_regs));

	regs->mepc     = epc;
	regs->mstatus  = status;
	regs->mstatusH = statusH;

	regs->a0 = a0; 
	regs->a1 = a1; 
	regs->a2 = a2; 
	regs->a3 = a3; 
	regs->a4 = a4; 
	regs->a5 = a5; 
	regs->a6 = a6; 
	regs->a7 = a7;

	return;
}

static inline void restore_registers_mmio_load(struct sbi_trap_regs *regs,
				     struct vcpu_state *state)
{
	ulong htinst = csr_read(CSR_HTINST);
	ulong insn = state->prev_mmio_insn;

	if (htinst & 0x1) {
		insn = htinst | INSN_16BIT_MASK;
	}

	/* Decode length of MMIO and shift */
	if ((insn & INSN_MASK_LW) == INSN_MATCH_LW) {
		// Pass
	}
#if __riscv_xlen == 64
	else if ((insn & INSN_MASK_C_LD) == INSN_MATCH_C_LD) {
		insn = RVC_RS2S(insn) << SH_RD;
	}
#endif
	else if ((insn & INSN_MASK_C_LW) == INSN_MATCH_C_LW) {
		insn = RVC_RS2S(insn) << SH_RD;
	}
	

	ulong epc     = regs->mepc;
	ulong status  = regs->mstatus;
	ulong statusH = regs->mstatusH;
	ulong saved_value = *REG_PTR(insn, SH_RD, regs);

	sbi_memcpy(regs, &state->vcpu_state, sizeof(struct sbi_trap_regs));

	regs->mepc     = epc;
	regs->mstatus  = status;
	regs->mstatusH = statusH;

	SET_RD(insn, regs, saved_value);

	return;
}


struct vcpu_state *sm_prepare_cpu(uint64_t cpu_id)
{
	if (cpu_id >= STORED_STATES) {
		return 0;
	}

	unsigned long vm_id = get_vm_id();

	struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

	scratch->vm_id	= vm_id;
	scratch->cpu_id = cpu_id;

	return get_vcpu_state(vm_id, cpu_id);
}

int sm_prepare_mmio(uint64_t cpu_id) {
	unsigned long vm_id = get_vm_id();

	struct vcpu_state *state = get_vcpu_state(vm_id, cpu_id);

	spin_lock(&state->lock);

	state->next_mmio = true;

	spin_unlock(&state->lock);

	return 0;
}

int sm_create_cpu(uint64_t cpu_id, const struct sbi_trap_regs *regs)
{
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
	state->trap.cause    = -1LLU;

	state->next_mmio = false;
	state->prev_mmio_insn = 0;

	mask_hgatp(state);

	SPIN_LOCK_INIT(state->lock);
	state->running = false;

	spin_unlock(&global_lock);

	return 0;
}

int sm_resume_cpu(uint64_t cpu_id, struct sbi_trap_regs *regs)
{
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
	case CAUSE_VIRTUAL_SUPERVISOR_ECALL: {
		restore_registers_ecall(regs, state);
		prepare_for_vm(regs, state);
	}
	break;
	case CAUSE_FETCH_ACCESS:
	case CAUSE_FETCH_GUEST_PAGE_FAULT: {
		restore_registers(regs, state);
		prepare_for_vm(regs, state);
	}
	break;
	case CAUSE_LOAD_GUEST_PAGE_FAULT:
	case CAUSE_STORE_GUEST_PAGE_FAULT: {
		if (state->prev_mmio_insn != 0) {
			if (state->trap.cause == CAUSE_STORE_GUEST_PAGE_FAULT) restore_registers(regs, state);
			else restore_registers_mmio_load(regs, state);

			state->prev_mmio_insn = 0;
		} else {
			restore_registers(regs, state);
		}
		
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
			sbi_printf("BRUH 0x%" PRILX "0x%" PRILX "\n", sepc,
				   state->vcpu_state.mepc);
			ret = SBI_EUNKNOWN;

			goto trap_error;
		}
	} break;

	case IRQ_S_SOFT_FLIPPED:
	case IRQ_S_TIMER_FLIPPED:
	case IRQ_S_EXT_FLIPPED:
	case IRQ_S_GEXT_FLIPPED: {
		if (sepc == state->vcpu_state.mepc) {
			restore_registers(regs, state);
			prepare_for_vm(regs, state);
		} else {
			sbi_printf("BRUH2 0x%" PRILX "0x%" PRILX "\n", sepc,
				   state->vcpu_state.mepc);
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

bool is_csr_fn(struct sbi_trap_info *trap)
{
	ulong insn = trap->tval;

	bool is_csr = false;
	const struct insn_match *ifn;
	for (int i = 0; i < sizeof(csr_functions) / sizeof(struct insn_match);
	     i++) {
		ifn = &csr_functions[i];
		if ((insn & ifn->mask) == ifn->match) {
			is_csr = true;
			break;
		}
	}

	return is_csr;
}

inline void hide_registers(struct sbi_trap_regs *regs,
			   struct sbi_trap_info *trap, struct vcpu_state *state,
			   bool is_virtual_insn_fault)
{
	ulong insn  = trap->tval;
	bool is_csr = is_virtual_insn_fault && is_csr_fn(trap);

	state->was_csr_insn = is_csr;
	ulong saved_value   = 0;

	if (is_csr && is_virtual_insn_fault)
		saved_value = GET_RS1(insn, regs);

	ulong epc     = regs->mepc;
	ulong status  = regs->mstatus;
	ulong statusH = regs->mstatusH;

	sbi_memset(regs, 0, sizeof(struct sbi_trap_regs));

	regs->mepc     = epc;
	regs->mstatus  = status;
	regs->mstatusH = statusH;

	if (is_csr && is_virtual_insn_fault)
		*REG_PTR(insn, SH_RS1, regs) = saved_value;
}

inline void hide_registers_ecall(struct sbi_trap_regs *regs,
			   struct sbi_trap_info *trap, struct vcpu_state *state)
{
	// sbi_printf("ECALL: %lu %lu\n", regs->a6, regs->a7);

	ulong epc     = regs->mepc;
	ulong status  = regs->mstatus;
	ulong statusH = regs->mstatusH;
	ulong a0 = regs->a0;
	ulong a1 = regs->a1;
	ulong a2 = regs->a2;
	ulong a3 = regs->a3;
	ulong a4 = regs->a4;
	ulong a5 = regs->a5;
	ulong a6 = regs->a6;
	ulong a7 = regs->a7;

	sbi_memset(regs, 0, sizeof(struct sbi_trap_regs));

	regs->mepc     = epc;
	regs->mstatus  = status;
	regs->mstatusH = statusH;

	regs->a0 = a0; 
	regs->a1 = a1; 
	regs->a2 = a2; 
	regs->a3 = a3; 
	regs->a4 = a4; 
	regs->a5 = a5; 
	regs->a6 = a6; 
	regs->a7 = a7; 
}

inline void hide_registers_mmio_store(struct sbi_trap_regs *regs,
			   struct vcpu_state *state, unsigned long insn) {
	ulong data = GET_RS2(insn, regs);


	if ((insn & INSN_MASK_SW) == INSN_MATCH_SW) {
		// empty case
	}
#if __riscv_xlen == 64
	if ((insn & INSN_MASK_C_SD) == INSN_MATCH_C_SD) {
		data = GET_RS2S(insn, regs);
	} else if ((insn & INSN_MASK_C_SDSP) == INSN_MATCH_C_SDSP &&
		   ((insn >> SH_RD) & 0x1f)) {
		data = GET_RS2C(insn, regs);
	}
#endif
	else if ((insn & INSN_MASK_C_SW) == INSN_MATCH_C_SW) {
		data = GET_RS2S(insn, regs);
	} else if ((insn & INSN_MASK_C_SWSP) == INSN_MATCH_C_SWSP &&
		   ((insn >> SH_RD) & 0x1f)) {
		data = GET_RS2C(insn, regs);
	}

	ulong epc     = regs->mepc;
	ulong status  = regs->mstatus;
	ulong statusH = regs->mstatusH;

	// sbi_memset(regs, 0, sizeof(struct sbi_trap_regs));

	regs->mepc     = epc;
	regs->mstatus  = status;
	regs->mstatusH = statusH;

	regs->a0 = data;
	regs->a1 = insn;

	state->prev_mmio_insn = insn;
}


inline void hide_registers_mmio_load(struct sbi_trap_regs *regs, struct vcpu_state *state, ulong insn)
{
	ulong epc     = regs->mepc;
	ulong status  = regs->mstatus;
	ulong statusH = regs->mstatusH;

	sbi_memset(regs, 0, sizeof(struct sbi_trap_regs));

	regs->mepc     = epc;
	regs->mstatus  = status;
	regs->mstatusH = statusH;

	regs->a1 = insn;

	state->prev_mmio_insn = insn;
}

int sm_preserve_cpu(struct sbi_trap_regs *regs, struct sbi_trap_info *trap)
{
	struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

	int cpu_id = scratch->cpu_id;
	int vm_id  = scratch->vm_id;

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

	csr_write(CSR_MIDELEG,
		  csr_read(CSR_MIDELEG) | MIP_SSIP | MIP_STIP | MIP_SEIP);
	csr_write(CSR_MEDELEG, state->prev_exception);

	switch (trap->cause) {
	case CAUSE_FETCH_ACCESS:
	case CAUSE_FETCH_GUEST_PAGE_FAULT: {
		hide_registers(regs, trap, state, false);
	}
	break;
	case CAUSE_LOAD_GUEST_PAGE_FAULT:
	case CAUSE_STORE_GUEST_PAGE_FAULT: {
		if (state->next_mmio) {
			state->next_mmio = false;
			struct sbi_trap_info utrap;
			ulong insn = sbi_get_insn(regs->mepc, &utrap);

			if (trap->cause == CAUSE_STORE_GUEST_PAGE_FAULT) hide_registers_mmio_store(regs, state, insn);
			else hide_registers_mmio_load(regs, state, insn);
		} else {
			hide_registers(regs, trap, state, false);
		}
	}
	break;

	case CAUSE_VIRTUAL_SUPERVISOR_ECALL: {
		hide_registers_ecall(regs, trap, state);
	}
	break;

	case CAUSE_VIRTUAL_INST_FAULT:
		hide_registers(regs, trap, state, true);
		break;
	default:
		hide_registers(regs, trap, state, false);
		break;
	}

	mask_hgatp(state);

	spin_unlock(&state->lock);

	return 0;
}

int get_page_num(uintptr_t pte_addr)
{
	if (unlikely(((hpt_start) <= pte_addr) && ((hpt_pmd_start) > pte_addr)))
		return 512 * 512;
	if (unlikely(((hpt_pmd_start) <= pte_addr) &&
		     ((hpt_pte_start) > pte_addr)))
		return 512;
	if (likely((hpt_pte_start <= pte_addr) && (pte_addr < hpt_end)))
		return 1;
	return -1;
}

/**
 * @brief Check if the action is valid, then perform it
 *
 * @param addr physical address of the entry
 * @param pte the new value of the entry
 * @param page_num The number of pages to be set
 * @return 0 on success, negative error code on failure
 */
inline int check_set_single_pte(unsigned long *addr, unsigned long pte,
				size_t page_num)
{
	if (unlikely(check_enabled == false)) {
		set_single_pte(addr, pte, page_num);
		return 0;
	}
	if (page_num < 0) {
		sbi_printf(
			"sm_set_pte: addr outside HPT (addr: 0x%lx, pte: 0x%lx, page_num: %lu)\n",
			(unsigned long)addr, pte, page_num);
		return -1;
	}
	if (pte & PTE_V) {
		if (page_num == 512 * 512) { // PGD
			uintptr_t nxt_pt = pte_to_phys(pte);
			if ((nxt_pt < hpt_pmd_start) ||
			    (nxt_pt >= hpt_pte_start)) {
				sbi_printf(
					"[%s] Invalid PGD entry(0x%lx): 0x%lx (a mapping to address 0x%lx), should be in [0x%lx, 0x%lx)\n",
					__func__, (uintptr_t)addr, pte, nxt_pt,
					hpt_pmd_start, hpt_pte_start);
				return -1;
			}
		} else if (page_num == 512 && !(pte & PTE_R) &&
			   !(pte & PTE_W) && !(pte & PTE_X)) { // non-leaf PMD
			uintptr_t nxt_pt = pte_to_phys(pte);
			if ((nxt_pt < hpt_pte_start) || (nxt_pt >= hpt_end)) {
				sbi_printf(
					"[%s] Invalid PMD entry(0x%lx): 0x%lx (a mapping to address 0x%lx), should be in [0x%lx, 0x%lx)\n",
					__func__, (uintptr_t)addr, pte, nxt_pt,
					hpt_pte_start, hpt_end);
				return -1;
			}
		} else { // leaf
			if (!test_public_shared_range(pte_to_ppn(pte),
						      page_num)) {
				sbi_printf(
					"Invalid page table leaf entry, contains private range(addr 0x%lx, pte 0x%lx, page_num %ld)\n",
					(uintptr_t)addr, pte, page_num);
				return -1;
			}
		}
	}

	return set_single_pte(addr, pte, page_num);
}

int sm_set_pte(unsigned long sub_fid, unsigned long *addr,
	       unsigned long pte_or_src, size_t size)
{
	int ret = 0;
	lock_bitmap;
	switch (sub_fid) {
	case SBI_EXT_SM_SET_PTE_CLEAR:
		for (size_t i = 0; i < size / sizeof(uintptr_t); ++i, ++addr) {
			set_single_pte(addr, 0, 0);
		}
		break;
	case SBI_EXT_SM_SET_PTE_MEMCPY:
		if (size % 8) {
			ret = -1;
			sbi_printf(
				"sm_set_pte: SBI_EXT_SM_SET_PTE_MEMCPY: size align failed (addr: 0x%lx, src: 0x%lx, size: %lu)\n",
				(unsigned long)addr, pte_or_src, size);
		}
		size_t page_num = size >> 3;
		for (size_t i = 0; i < page_num; ++i, ++addr) {
			uintptr_t pte = *((uintptr_t *)pte_or_src + i);
			ret	      = check_set_single_pte(
				  addr, pte, get_page_num((uintptr_t)addr));
			if (unlikely(ret))
				break;
		}
		break;
	case SBI_EXT_SM_SET_PTE_SET_ONE:
		ret = check_set_single_pte(addr, pte_or_src,
					   get_page_num((uintptr_t)addr));
		break;
	default:
		ret = -1;
		break;
	}
	unlock_bitmap;
	return ret;
}
