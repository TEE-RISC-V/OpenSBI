#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_trap.h>
#include <sm/sm.h>

static int sbi_ecall_sm_handler(unsigned long extid, unsigned long funcid,
				struct sbi_trap_regs *regs,
				unsigned long *out_val,
				struct sbi_trap_info *out_trap)
{
	int ret = 0;

	switch (funcid) {
	case SBI_EXT_SM_SET_GUEST_BOUNCE_BUFFER:
#if __riscv_xlen == 32
	bool virt = (regs->mstatusH & MSTATUSH_MPV) ? TRUE : FALSE;
#else
	bool virt = (regs->mstatus & MSTATUS_MPV) ? TRUE : FALSE;
#endif
		if (unlikely(!virt)) {
			ret = -1;
		} else {
			ret = sm_set_bounce_buffer(regs->a0, regs->a1);
		}
		break;
	case SBI_EXT_SM_BITMAP_AND_HPT_INIT:
		ret = bitmap_and_hpt_init(regs->a0, regs->a1, regs->a2,
					  regs->a3, regs->a4, regs->a5);
		break;
	case SBI_EXT_SM_SET_PTE:
		ret = sm_set_pte(regs->a0, (unsigned long *)regs->a1, regs->a2,
				 regs->a3);
		break;
	case SBI_EXT_SM_MONITOR_INIT:
		ret = monitor_init(&regs->mstatus);
		break;
	default:
		sbi_printf(
			"SBI_ENOTSUPP: extid 0x%lx, funcid 0x%lx, a0 0x%lx, a1 0x%lx, a2 0x%lx, a3 0x%lx, a4 0x%lx, a5 0x%lx\n",
			extid, funcid, regs->a0, regs->a1, regs->a2, regs->a3,
			regs->a4, regs->a5);
		ret = SBI_ENOTSUPP;
	}
	return ret;
}

struct sbi_ecall_extension ecall_sm = {
	.extid_start = SBI_EXT_SM,
	.extid_end   = SBI_EXT_SM,
	.handle	     = sbi_ecall_sm_handler,
};
