#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_trap.h>
#include <sm/sm.h>

static int sbi_ecall_sm_handler(unsigned long extid, unsigned long funcid,
				const struct sbi_trap_regs *regs,
				unsigned long *out_val,
				struct sbi_trap_info *out_trap)
{
	int ret = 0;

	switch (funcid) {
	case SBI_EXT_SM_SET_SHARED:
		ret = sm_set_shared(regs->a0, regs->a1);
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
		ret = monitor_init();
		break;
	default:
		ret = SBI_ENOTSUPP;
	}
	return ret;
}

struct sbi_ecall_extension ecall_sm = {
	.extid_start = SBI_EXT_SM,
	.extid_end   = SBI_EXT_SM,
	.handle	     = sbi_ecall_sm_handler,
};
