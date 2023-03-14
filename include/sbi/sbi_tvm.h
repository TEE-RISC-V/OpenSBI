#ifndef __SBI_TVM_H__
#define __SBI_TVM_H__

#include <sbi/sbi_types.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/riscv_asm.h>

struct tvm_data_t {
	struct sbi_hartmask smask;
};

int sbi_tvm_init(struct sbi_scratch *scratch, bool cold_boot);

int sbi_send_tvm();

int set_tvm_and_sync();

#endif
