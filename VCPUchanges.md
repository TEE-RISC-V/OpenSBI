Summary of changes for VCPU
==========================

Important changes:

* new information stored in the scratch space (/include/sbi/sbi_scratch.h)
* necessary fields are initialized in sbi_scratch_init (sbi_scratch.c)
* delegate_traps changed in sbi_hart.c to make m-mode handle everything by default


Main change in sbi_trap.c:

* When a VM exception is redirected to supervisor, the registers are copied into scratch space
* For certain cases, some registers are then zeroed out depending on what the supervisor needs to emulate the instruction
* mstatus TSR bit it set so that SM can trap the supervisor's sret 
* When intercepting an sret instruction, SM takes original register values (stored in scratch), and merges with the result registers from the supervisor


Minor changes:
* definitions in include/sbi/riscv_encoding.h
* sbi_expected_trap.S has a global reference to a single "sret" instruction so that sbi_trap can redirect to it