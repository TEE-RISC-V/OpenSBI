/*
 * SPDX-License-Identifier: BSD-2-Clause
 *
 * Copyright (c) 2019 Western Digital Corporation or its affiliates.
 *
 * Authors:
 *   Anup Patel <anup.patel@wdc.com>
 */

#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/sbi_bitops.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_ecall.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_illegal_insn.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_irqchip.h>
#include <sbi/sbi_misaligned_ldst.h>
#include <sbi/sbi_pmu.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_timer.h>
#include <sbi/sbi_trap.h>
#include <sbi/sbi_string.h>

extern void __sbi_just_sret();

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

static void __noreturn sbi_trap_error(const char *msg, int rc,
				      ulong mcause, ulong mtval, ulong mtval2,
				      ulong mtinst, struct sbi_trap_regs *regs)
{
	u32 hartid = current_hartid();

	sbi_printf("%s: hart%d: %s (error %d)\n", __func__, hartid, msg, rc);
	sbi_printf("%s: hart%d: mcause=0x%" PRILX " mtval=0x%" PRILX "\n",
		   __func__, hartid, mcause, mtval);
	if (misa_extension('H')) {
		sbi_printf("%s: hart%d: mtval2=0x%" PRILX
			   " mtinst=0x%" PRILX "\n",
			   __func__, hartid, mtval2, mtinst);
	}
	sbi_printf("%s: hart%d: mepc=0x%" PRILX " mstatus=0x%" PRILX "\n",
		   __func__, hartid, regs->mepc, regs->mstatus);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "ra", regs->ra, "sp", regs->sp);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "gp", regs->gp, "tp", regs->tp);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s0", regs->s0, "s1", regs->s1);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a0", regs->a0, "a1", regs->a1);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a2", regs->a2, "a3", regs->a3);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a4", regs->a4, "a5", regs->a5);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "a6", regs->a6, "a7", regs->a7);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s2", regs->s2, "s3", regs->s3);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s4", regs->s4, "s5", regs->s5);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s6", regs->s6, "s7", regs->s7);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s8", regs->s8, "s9", regs->s9);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "s10", regs->s10, "s11", regs->s11);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "t0", regs->t0, "t1", regs->t1);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "t2", regs->t2, "t3", regs->t3);
	sbi_printf("%s: hart%d: %s=0x%" PRILX " %s=0x%" PRILX "\n", __func__,
		   hartid, "t4", regs->t4, "t5", regs->t5);
	sbi_printf("%s: hart%d: %s=0x%" PRILX "\n", __func__, hartid, "t6",
		   regs->t6);

	sbi_hart_hang();
}

/**
 * Redirect trap to lower privledge mode (S-mode or U-mode)
 *
 * @param regs pointer to register state
 * @param trap pointer to trap details
 *
 * @return 0 on success and negative error code on failure
 */
int sbi_trap_redirect(struct sbi_trap_regs *regs,
		      struct sbi_trap_info *trap)
{
	ulong hstatus, vsstatus, prev_mode;
#if __riscv_xlen == 32
	bool prev_virt = (regs->mstatusH & MSTATUSH_MPV) ? TRUE : FALSE;
#else
	bool prev_virt = (regs->mstatus & MSTATUS_MPV) ? TRUE : FALSE;
#endif
	/* By default, we redirect to HS-mode */
	bool next_virt = FALSE;

	/* Sanity check on previous mode */
	prev_mode = (regs->mstatus & MSTATUS_MPP) >> MSTATUS_MPP_SHIFT;
	if (prev_mode != PRV_S && prev_mode != PRV_U)
		return SBI_ENOTSUPP;

	/* If exceptions came from VS/VU-mode, redirect to VS-mode if
	 * delegated in hedeleg
	 */
	if (misa_extension('H') && prev_virt) {
		if ((trap->cause < __riscv_xlen) &&
		    (csr_read(CSR_HEDELEG) & BIT(trap->cause))) {
			next_virt = TRUE;
		}
	}

	/* Update MSTATUS MPV bits */
#if __riscv_xlen == 32
	regs->mstatusH &= ~MSTATUSH_MPV;
	regs->mstatusH |= (next_virt) ? MSTATUSH_MPV : 0UL;
#else
	regs->mstatus &= ~MSTATUS_MPV;
	regs->mstatus |= (next_virt) ? MSTATUS_MPV : 0UL;
#endif

	/* Update hypervisor CSRs if going to HS-mode */
	// TODO: make this use a fixed constant rather than something that can a CSR modified by the hypervisor
	if (misa_extension('H') && !next_virt) {
		hstatus = csr_read(CSR_HSTATUS);
		if (prev_virt) {
			/* hstatus.SPVP is only updated if coming from VS/VU-mode */
			hstatus &= ~HSTATUS_SPVP;
			hstatus |= (prev_mode == PRV_S) ? HSTATUS_SPVP : 0;
		}
		hstatus &= ~HSTATUS_SPV;
		hstatus |= (prev_virt) ? HSTATUS_SPV : 0;
		hstatus &= ~HSTATUS_GVA;
		hstatus |= (trap->gva) ? HSTATUS_GVA : 0;
		csr_write(CSR_HSTATUS, hstatus);
		csr_write(CSR_HTVAL, trap->tval2);
		csr_write(CSR_HTINST, trap->tinst);
	}

	/* Update exception related CSRs */
	if (next_virt) {
		/* Update VS-mode exception info */
		csr_write(CSR_VSTVAL, trap->tval);
		csr_write(CSR_VSEPC, trap->epc);
		csr_write(CSR_VSCAUSE, trap->cause);

		/* Set MEPC to VS-mode exception vector base */
		regs->mepc = csr_read(CSR_VSTVEC);

		/* Set MPP to VS-mode */
		regs->mstatus &= ~MSTATUS_MPP;
		regs->mstatus |= (PRV_S << MSTATUS_MPP_SHIFT);

		/* Get VS-mode SSTATUS CSR */
		vsstatus = csr_read(CSR_VSSTATUS);

		/* Set SPP for VS-mode */
		vsstatus &= ~SSTATUS_SPP;
		if (prev_mode == PRV_S)
			vsstatus |= (1UL << SSTATUS_SPP_SHIFT);

		/* Set SPIE for VS-mode */
		vsstatus &= ~SSTATUS_SPIE;
		if (vsstatus & SSTATUS_SIE)
			vsstatus |= (1UL << SSTATUS_SPIE_SHIFT);

		/* Clear SIE for VS-mode */
		vsstatus &= ~SSTATUS_SIE;

		/* Update VS-mode SSTATUS CSR */
		csr_write(CSR_VSSTATUS, vsstatus);
	} else {
		struct sbi_scratch *scratch;

		if (prev_virt) {
			scratch = sbi_scratch_thishart_ptr();
			scratch->storing_vcpu = 1;

			sbi_memcpy(&scratch->state.vcpu_state, regs, sizeof(struct sbi_trap_regs));
			sbi_memcpy(&scratch->state.trap, trap, sizeof(struct sbi_trap_info));

			// TODO: implement all the cases
			switch (trap->cause) {
			case CAUSE_VIRTUAL_INST_FAULT:
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

				scratch->state.was_csr_insn = is_csr;
				ulong saved_value = 0;

				// TODO: clean up this code
				if (is_csr) {
					saved_value = GET_RS1(insn, regs);	

					ulong epc = regs->mepc;
					ulong status = regs->mstatus;
					ulong statusH = regs->mstatusH;

					sbi_memset(regs, 0, sizeof(struct sbi_trap_regs));

					regs->mepc = epc;
					regs->mstatus = status;
					regs->mstatusH = statusH;

					*REG_PTR(insn, SH_RS1, regs) = saved_value;
				} else {
					ulong epc = regs->mepc;
					ulong status = regs->mstatus;
					ulong statusH = regs->mstatusH;

					sbi_memset(regs, 0, sizeof(struct sbi_trap_regs));

					regs->mepc = epc;
					regs->mstatus = status;
					regs->mstatusH = statusH;

				}

				break;
			}
		}


		/* Update S-mode exception info */
		csr_write(CSR_STVAL, trap->tval);
		csr_write(CSR_SEPC, trap->epc);
		csr_write(CSR_SCAUSE, trap->cause);

		/* Set MEPC to S-mode exception vector base */
		regs->mepc = csr_read(CSR_STVEC);

		/* Set MPP to S-mode */
		regs->mstatus &= ~MSTATUS_MPP;
		regs->mstatus |= (PRV_S << MSTATUS_MPP_SHIFT);

		/* Set SPP for S-mode */
		regs->mstatus &= ~MSTATUS_SPP;
		if (prev_mode == PRV_S)
			regs->mstatus |= (1UL << MSTATUS_SPP_SHIFT);

		/* Set SPIE for S-mode */
		regs->mstatus &= ~MSTATUS_SPIE;
		if (regs->mstatus & MSTATUS_SIE)
			regs->mstatus |= (1UL << MSTATUS_SPIE_SHIFT);

		/* Clear SIE for S-mode */
		regs->mstatus &= ~MSTATUS_SIE;
	}

	return 0;
}

static int sbi_trap_nonaia_irq(struct sbi_trap_regs *regs, ulong mcause)
{
	mcause &= ~(1UL << (__riscv_xlen - 1));
	switch (mcause) {
	case IRQ_M_TIMER:
		sbi_timer_process();
		break;
	case IRQ_M_SOFT:
		sbi_ipi_process();
		break;
	case IRQ_M_EXT:
		return sbi_irqchip_process(regs);
	default:
		return SBI_ENOENT;
	};

	return 0;
}

static int sbi_trap_aia_irq(struct sbi_trap_regs *regs, ulong mcause)
{
	int rc;
	unsigned long mtopi;

	while ((mtopi = csr_read(CSR_MTOPI))) {
		mtopi = mtopi >> TOPI_IID_SHIFT;
		switch (mtopi) {
		case IRQ_M_TIMER:
			sbi_timer_process();
			break;
		case IRQ_M_SOFT:
			sbi_ipi_process();
			break;
		case IRQ_M_EXT:
			rc = sbi_irqchip_process(regs);
			if (rc)
				return rc;
			break;
		default:
			return SBI_ENOENT;
		}
	}

	return 0;
}

/**
 * Handle trap/interrupt
 *
 * This function is called by firmware linked to OpenSBI
 * library for handling trap/interrupt. It expects the
 * following:
 * 1. The 'mscratch' CSR is pointing to sbi_scratch of current HART
 * 2. The 'mcause' CSR is having exception/interrupt cause
 * 3. The 'mtval' CSR is having additional trap information
 * 4. The 'mtval2' CSR is having additional trap information
 * 5. The 'mtinst' CSR is having decoded trap instruction
 * 6. Stack pointer (SP) is setup for current HART
 * 7. Interrupts are disabled in MSTATUS CSR
 *
 * @param regs pointer to register state
 */
struct sbi_trap_regs *sbi_trap_handler(struct sbi_trap_regs *regs)
{
	int rc = SBI_ENOTSUPP;
	const char *msg = "trap handler failed";
	ulong mcause = csr_read(CSR_MCAUSE);
	ulong mtval = csr_read(CSR_MTVAL), mtval2 = 0, mtinst = 0;
	struct sbi_trap_info trap;

	if (misa_extension('H')) {
		mtval2 = csr_read(CSR_MTVAL2);
		mtinst = csr_read(CSR_MTINST);
	}

	if (mcause & (1UL << (__riscv_xlen - 1))) {
		if (sbi_hart_has_extension(sbi_scratch_thishart_ptr(),
					   SBI_HART_EXT_SMAIA))
			rc = sbi_trap_aia_irq(regs, mcause);
		else
			rc = sbi_trap_nonaia_irq(regs, mcause);
		if (rc) {
			msg = "unhandled local interrupt";
			goto trap_error;
		}
		return regs;
	}

#if __riscv_xlen == 32
	bool prev_virt = (regs->mstatusH & MSTATUSH_MPV) ? TRUE : FALSE;
#else
	bool prev_virt = (regs->mstatus & MSTATUS_MPV) ? TRUE : FALSE;
#endif

	if (mcause == CAUSE_ILLEGAL_INSTRUCTION && mtval == INSN_SRET && !prev_virt) {
		struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

		if (scratch->storing_vcpu) {
			struct vcpu_state *state = &scratch->state;
			switch (state->trap.cause) {
				case CAUSE_VIRTUAL_INST_FAULT:
					ulong sepc = csr_read(CSR_SEPC);
					// Supervisor trying to return to next instruction

					if (sepc == state->vcpu_state.mepc + 4) {
						regs->mstatus &= ~MSTATUS_TSR;
						scratch->storing_vcpu = 0;

						// TODO: clean up this code
						if (state->was_csr_insn) {
							ulong orig_insn = state->trap.tval;

							ulong reg_value = *REG_PTR(orig_insn, SH_RD, regs);

							ulong epc = regs->mepc;
							ulong status = regs->mstatus;
							ulong statusH = regs->mstatusH;

							sbi_memcpy(regs, &state->vcpu_state, sizeof(struct sbi_trap_regs));

							regs->mepc = epc;
							regs->mstatus = status;
							regs->mstatusH = statusH;

							SET_RD(orig_insn, regs, reg_value);
						} else {
							// sbi_printf("HELLOTHERE\n");
							ulong epc = regs->mepc;
							ulong status = regs->mstatus;
							ulong statusH = regs->mstatusH;

							sbi_memcpy(regs, &state->vcpu_state, sizeof(struct sbi_trap_regs));

							regs->mepc = epc;
							regs->mstatus = status;
							regs->mstatusH = statusH;
						}
					} else if (sepc == csr_read(CSR_VSTVEC)) {
						// sbi_printf("HELLOTHERE 2\n");
						// Supervisor trying to redirect to supervisor trap handler
						regs->mstatus &= ~MSTATUS_TSR;
						scratch->storing_vcpu = 0;

						ulong epc = regs->mepc;
						ulong status = regs->mstatus;
						ulong statusH = regs->mstatusH;

						sbi_memcpy(regs, &state->vcpu_state, sizeof(struct sbi_trap_regs));

						regs->mepc = epc;
						regs->mstatus = status;
						regs->mstatusH = statusH;

						// Redirecting, restore all cpu state I guess
					} else {
						sbi_printf("BRUH 0x%" PRILX "0x%" PRILX "\n", sepc, state->vcpu_state.mepc);
						// sbi_printf("does this still happen...\n");
						// regs->mstatus = INSERT_FIELD(regs->mstatus, MSTATUS_MPP, PRV_M) ;
						// regs->mstatus = INSERT_FIELD(regs->mstatus, MSTATUS_MPIE, 0);
						// regs->mepc = (ulong) &__sbi_just_sret;

						// TODO: make sure this happens only once per vCPU, after it has been initialized
						regs->mstatus &= ~MSTATUS_TSR;
						scratch->storing_vcpu = 0;
					}
				break;

				case CAUSE_FETCH_GUEST_PAGE_FAULT:
				case CAUSE_STORE_GUEST_PAGE_FAULT:
				case CAUSE_LOAD_GUEST_PAGE_FAULT:
					regs->mstatus &= ~MSTATUS_TSR;
					scratch->storing_vcpu = 0;
				break;

				default:
					regs->mstatus &= ~MSTATUS_TSR;
					scratch->storing_vcpu = 0;
					break;
			}
			return regs;
		}
	}


	switch (mcause) {
	case CAUSE_ILLEGAL_INSTRUCTION:
		rc  = sbi_illegal_insn_handler(mtval, regs);
		msg = "illegal instruction handler failed";
		break;
	case CAUSE_MISALIGNED_LOAD:
		rc = sbi_misaligned_load_handler(mtval, mtval2, mtinst, regs);
		msg = "misaligned load handler failed";
		break;
	case CAUSE_MISALIGNED_STORE:
		rc  = sbi_misaligned_store_handler(mtval, mtval2, mtinst, regs);
		msg = "misaligned store handler failed";
		break;
	case CAUSE_SUPERVISOR_ECALL:
	case CAUSE_MACHINE_ECALL:
		rc  = sbi_ecall_handler(regs);
		msg = "ecall handler failed";
		break;
	case CAUSE_LOAD_ACCESS:
	case CAUSE_STORE_ACCESS:
		sbi_pmu_ctr_incr_fw(mcause == CAUSE_LOAD_ACCESS ?
			SBI_PMU_FW_ACCESS_LOAD : SBI_PMU_FW_ACCESS_STORE);
		/* fallthrough */
	default:
		/* If the trap came from S or U mode, redirect it there */
		trap.epc = regs->mepc;
		trap.cause = mcause;
		trap.tval = mtval;
		trap.tval2 = mtval2;
		trap.tinst = mtinst;
		trap.gva   = sbi_regs_gva(regs);

		rc = sbi_trap_redirect(regs, &trap);
		break;
	};

trap_error:
	if (rc)
		sbi_trap_error(msg, rc, mcause, mtval, mtval2, mtinst, regs);
	return regs;
}

typedef void (*trap_exit_t)(const struct sbi_trap_regs *regs);

/**
 * Exit trap/interrupt handling
 *
 * This function is called by non-firmware code to abruptly exit
 * trap/interrupt handling and resume execution at context pointed
 * by given register state.
 *
 * @param regs pointer to register state
 */
void __noreturn sbi_trap_exit(const struct sbi_trap_regs *regs)
{
	struct sbi_scratch *scratch = sbi_scratch_thishart_ptr();

	((trap_exit_t)scratch->trap_exit)(regs);
	__builtin_unreachable();
}
