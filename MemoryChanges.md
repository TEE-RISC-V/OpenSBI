# Summary of Changes for Memory Management

## SBI interfaces

* Function prototypes in include/sm/sm.h
* If you want to call them, please use the eid and fid defined in include/sbi/riscv_encoding.h
* Implementations in sbi_ecall_sm.c and sm/sm.c

## TVM trap

* We are going to emulate TVM trap in sbi_trap_handler (sbi_trap.c)
