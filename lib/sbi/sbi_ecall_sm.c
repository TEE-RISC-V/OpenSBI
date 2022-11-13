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