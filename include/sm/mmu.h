#ifndef __MMU_H__
#define __MMU_H__

#include <sbi/sbi_types.h>
#include <sbi/sbi_trap.h>
#include <sbi/riscv_asm.h>
#include <sbi/riscv_encoding.h>
#include <sbi/riscv_fp.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_pmu.h>
#include <sbi/sbi_unpriv.h>

int set_private(uint64_t pfn_start, uint64_t num);
int sbi_access_handler(ulong addr, ulong tval2, ulong tinst,
		       struct sbi_trap_regs *regs);

#endif
