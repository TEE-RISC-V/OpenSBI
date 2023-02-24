#include <sbi/sbi_ecall.h>
#include <sbi/sbi_ecall_interface.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_trap.h>
#include <sm/sm.h>

static int sbi_ecall_sm_create_handler(unsigned long extid, unsigned long funcid,
				struct sbi_trap_regs *regs,
				unsigned long *out_val,
				struct sbi_trap_info *out_trap)
{
	int ret = 0;

	ret = sm_create_cpu(regs->a1, regs);

	return ret;
}


struct sbi_ecall_extension ecall_sm_create = {
	.extid_start = SBI_EXT_SM_CREATE,
	.extid_end   = SBI_EXT_SM_CREATE,
	.handle	     = sbi_ecall_sm_create_handler,
};