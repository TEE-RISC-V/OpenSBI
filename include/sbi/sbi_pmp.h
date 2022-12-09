#ifndef __SBI_PMP_H__
#define __SBI_PMP_H__

#include <sbi/sbi_types.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/riscv_asm.h>

struct pmp_config_t {
	unsigned int n;
	unsigned long prot;
	unsigned long addr;
	unsigned long log2len;
};
struct pmp_data_t {
	struct pmp_config_t pmp_config_arg;
	struct sbi_hartmask smask;
};

struct sbi_scratch;

int sbi_pmp_init(struct sbi_scratch *scratch, bool cold_boot);

int sbi_send_pmp(ulong hmask, ulong hbase, struct pmp_data_t *pmp_data);

int set_pmp_and_sync(unsigned int n, unsigned long prot, unsigned long addr,
		     unsigned long log2len);

#endif
