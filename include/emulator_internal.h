#ifndef EMULATOR_INTERNAL_H
#define EMULATOR_INTERNAL_H

#include "emulator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

FILE *emulator_trace_stream(Emulator *emu);
bool check_syscall_buffer(const Memory *memory, uint64_t address, uint64_t length, char *error,
                          size_t error_size);

const char *exception_cause_name(EmuExceptionCause cause);
bool exception_cause_from_memory_fault(const EmuFault *fault, const EmuDecodedInstruction *instruction,
                                       EmuExceptionCause *cause);
EmuExceptionCause fetch_exception_cause_from_memory_fault(const EmuFault *fault);
bool exception_fault_address_from_instruction(const Cpu *cpu, const Memory *memory,
                                              const EmuDecodedInstruction *instruction, uint64_t *address);
EmuStatus maybe_raise_exception(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
                                uint64_t interrupted_pc, uint64_t resume_pc, char *error, size_t error_size);
EmuStatus sample_pending_interrupt(Emulator *emu, char *error, size_t error_size);
void update_timer_interrupt_deadline(Emulator *emu);

bool toy_kernel_wake_sleepers(Emulator *emu);
bool toy_kernel_running_task(const EmuToyKernel *kernel);
EmuStatus toy_kernel_schedule_after_current(Emulator *emu, uint64_t resume_pc, bool exiting, uint64_t exit_code,
                                            char *error, size_t error_size);
EmuStatus toy_kernel_fault_current_task(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
                                        uint64_t interrupted_pc, uint64_t resume_pc, char *error,
                                        size_t error_size);
bool toy_kernel_is_trap(uint64_t imm);
EmuStatus toy_kernel_handle_brk(Emulator *emu, const EmuDecodedInstruction *instruction, uint64_t current_pc,
                                char *error, size_t error_size);
EmuStatus toy_kernel_handle_kernel_brk(Emulator *emu, const EmuDecodedInstruction *instruction, uint64_t current_pc,
                                       char *error, size_t error_size);

#endif
