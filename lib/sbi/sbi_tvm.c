#include <sbi/riscv_asm.h>
#include <sbi/riscv_atomic.h>
#include <sbi/riscv_barrier.h>
#include <sbi/sbi_error.h>
#include <sbi/sbi_fifo.h>
#include <sbi/sbi_hart.h>
#include <sbi/sbi_ipi.h>
#include <sbi/sbi_scratch.h>
#include <sbi/sbi_tlb.h>
#include <sbi/sbi_hfence.h>
#include <sbi/sbi_string.h>
#include <sbi/sbi_console.h>
#include <sbi/sbi_platform.h>
#include <sbi/sbi_hartmask.h>
#include <sbi/sbi_tvm.h>

static unsigned long tvm_data_offset;
static unsigned long tvm_sync_offset;

inline void tvm_set()
{
	uintptr_t mstatus = csr_read(CSR_MSTATUS);

	u32 curr_hartid = current_hartid();
	sbi_printf("tvm_set[%u]: old mstatus 0x%lx\n", curr_hartid, mstatus);

	mstatus = INSERT_FIELD(mstatus, MSTATUS_TVM, 1);
	sbi_printf("tvm_set[%u]: supposed new mstatus 0x%lx\n", curr_hartid,
		   mstatus);
	csr_write(CSR_MSTATUS, mstatus);

        uintptr_t medeleg = csr_read(CSR_MEDELEG);
	sbi_printf("tvm_set[%u]: medeleg 0x%lx\n", curr_hartid, medeleg);
	mstatus = csr_read(CSR_MSTATUS);
	sbi_printf("tvm_set[%u]: new mstatus 0x%lx\n", curr_hartid, mstatus);
}

static void sbi_process_tvm(struct sbi_scratch *scratch)
{
	struct tvm_data_t *data =
		sbi_scratch_offset_ptr(scratch, tvm_data_offset);
	struct sbi_scratch *rscratch = NULL;
	u32 rhartid;
	unsigned long *tvm_sync = NULL;
	tvm_set();

	// sync
	sbi_hartmask_for_each_hart(rhartid, &data->smask)
	{
		rscratch = sbi_hartid_to_scratch(rhartid);
		if (!rscratch)
			continue;
		tvm_sync = sbi_scratch_offset_ptr(rscratch, tvm_sync_offset);
		while (atomic_raw_xchg_ulong(tvm_sync, 1))
			;
	}
}

static int sbi_update_tvm(struct sbi_scratch *scratch,
			  struct sbi_scratch *remote_scratch, u32 remote_hartid,
			  void *data)
{
	struct tvm_data_t *tvm_data = NULL;
	u32 curr_hartid		    = current_hartid();

	if (remote_hartid == curr_hartid) {
		// update the tvm register locally
		tvm_set();
		return -1;
	}

	tvm_data = sbi_scratch_offset_ptr(remote_scratch, tvm_data_offset);
	// update the remote hart tvm data
	sbi_memcpy(tvm_data, data, sizeof(struct tvm_data_t));

	return 0;
}

static void sbi_tvm_sync(struct sbi_scratch *scratch)
{
	unsigned long *tvm_sync =
		sbi_scratch_offset_ptr(scratch, tvm_sync_offset);
	// wait the remote hart process the tvm signal
	while (!atomic_raw_xchg_ulong(tvm_sync, 0))
		;
	return;
}

static struct sbi_ipi_event_ops tvm_ops = {
	.name	 = "IPI_TVM",
	.update	 = sbi_update_tvm,
	.sync	 = sbi_tvm_sync,
	.process = sbi_process_tvm,
};

static u32 tvm_event = SBI_IPI_EVENT_MAX;

int sbi_send_tvm(ulong hmask, ulong hbase, struct tvm_data_t *tvm_data)
{
	return sbi_ipi_send_many(hmask, hbase, tvm_event, tvm_data);
}

int sbi_tvm_init(struct sbi_scratch *scratch, bool cold_boot)
{
	int ret;
	struct tvm_data_t *tvmdata;
	unsigned long *tvm_sync;

	if (cold_boot) {
		// Define the tvm data offset in the scratch
		tvm_data_offset = sbi_scratch_alloc_offset(sizeof(*tvmdata));
		if (!tvm_data_offset)
			return SBI_ENOMEM;

		tvm_sync_offset = sbi_scratch_alloc_offset(sizeof(*tvm_sync));
		if (!tvm_sync_offset)
			return SBI_ENOMEM;

		tvmdata = sbi_scratch_offset_ptr(scratch, tvm_data_offset);

		tvm_sync = sbi_scratch_offset_ptr(scratch, tvm_sync_offset);

		*tvm_sync = 0;

		ret = sbi_ipi_event_create(&tvm_ops);
		if (ret < 0) {
			sbi_scratch_free_offset(tvm_data_offset);
			return ret;
		}
		tvm_event = ret;
	} else {
	}

	return 0;
}

int set_tvm_and_sync()
{
	struct tvm_data_t tvm_data;
	u32 source_hart = current_hartid();

	// set current hart's tvm
	tvm_set();

	// sync all other harts
	SBI_HARTMASK_INIT_EXCEPT(&(tvm_data.smask), source_hart);
	sbi_send_tvm(0xFFFFFFFF & (~(1 << source_hart)), 0, &tvm_data);
	return 0;
}
