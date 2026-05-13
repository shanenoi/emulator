#ifndef EMULATOR_H
#define EMULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EMU_MEMORY_SIZE (1024u * 1024u)
#define EMU_LOAD_ADDRESS 0x1000ull
#define EMU_DEFAULT_INSTRUCTION_LIMIT 1000000ull
#define EMU_MAX_BREAKPOINTS 64u
#define EMU_PAGE_SIZE 4096u
#define EMU_STACK_SIZE (64u * 1024u)
#define EMU_STACK_GUARD_PAGES 1u
#define EMU_MAX_MEMORY_MAPPINGS 32u

#define EMU_DEVICE_UART_BASE 0x09000000ull
#define EMU_DEVICE_TIMER_BASE 0x09010000ull
#define EMU_DEVICE_RANDOM_BASE 0x09020000ull
#define EMU_DEVICE_EXCEPTION_BASE 0x09030000ull
#define EMU_DEVICE_SIZE 0x00001000ull

#define EMU_UART_DATA_OFFSET 0x00ull
#define EMU_UART_STATUS_OFFSET 0x04ull
#define EMU_TIMER_TICKS_LO_OFFSET 0x00ull
#define EMU_TIMER_TICKS_HI_OFFSET 0x04ull
#define EMU_TIMER_RESET_OFFSET 0x08ull
#define EMU_RANDOM_VALUE_OFFSET 0x00ull
#define EMU_RANDOM_SEED_OFFSET 0x04ull
#define EMU_EXCEPTION_VECTOR_OFFSET 0x00ull
#define EMU_EXCEPTION_CONTROL_OFFSET 0x08ull
#define EMU_EXCEPTION_TIMER_INTERVAL_OFFSET 0x10ull
#define EMU_EXCEPTION_PENDING_OFFSET 0x18ull
#define EMU_EXCEPTION_CAUSE_OFFSET 0x20ull
#define EMU_EXCEPTION_FAULT_ADDRESS_OFFSET 0x28ull
#define EMU_EXCEPTION_INTERRUPTED_PC_OFFSET 0x30ull
#define EMU_EXCEPTION_RESUME_PC_OFFSET 0x38ull
#define EMU_EXCEPTION_DEPTH_OFFSET 0x40ull

#define EMU_EXCEPTION_CONTROL_VECTOR_ENABLE 0x1u
#define EMU_EXCEPTION_CONTROL_INTERRUPTS_ENABLE 0x2u
#define EMU_EXCEPTION_CONTROL_QUEUE_TIMER 0x4u
#define EMU_EXCEPTION_CONTROL_CLEAR_PENDING 0x8u

#define EMU_MAP_READ 0x1u
#define EMU_MAP_WRITE 0x2u
#define EMU_MAP_EXEC 0x4u

typedef enum {
    EMU_OK = 0,
    EMU_ERROR = 1,
    EMU_HALTED = 2,
} EmuStatus;

#define EMU_SYSCALL_WRITE 64ull
#define EMU_SYSCALL_EXIT 93ull

#define EMU_SYSCALL_EIO (-5ll)
#define EMU_SYSCALL_EBADF (-9ll)
#define EMU_SYSCALL_ENOSYS (-38ll)

#define EMU_MAX_LOAD_SEGMENTS 16u
#define EMU_MAX_ELF_SEGMENTS EMU_MAX_LOAD_SEGMENTS
#define EMU_MAX_MACHO_SYMBOLS 8u
#define EMU_MAX_MACHO_SYMBOL_NAME 64u

#define EMU_ELF_MAGIC0 0x7fu
#define EMU_ELF_MAGIC1 'E'
#define EMU_ELF_MAGIC2 'L'
#define EMU_ELF_MAGIC3 'F'
#define EMU_ELF_CLASS_64 2u
#define EMU_ELF_DATA_LSB 1u
#define EMU_ELF_VERSION_CURRENT 1u
#define EMU_ELF_ET_EXEC 2u
#define EMU_ELF_ET_DYN 3u
#define EMU_ELF_EM_AARCH64 183u
#define EMU_ELF_PT_LOAD 1u
#define EMU_ELF_PT_INTERP 3u

#define EMU_ELF_PF_X 1u
#define EMU_ELF_PF_W 2u
#define EMU_ELF_PF_R 4u

#define EMU_MACHO_MAGIC_64 0xfeedfacfu
#define EMU_MACHO_MAGIC_32 0xfeedfaceu
#define EMU_MACHO_CIGAM_64 0xcffaedfeu
#define EMU_MACHO_CIGAM_32 0xcefaedfeu
#define EMU_MACHO_FAT_MAGIC 0xbebafecau
#define EMU_MACHO_FAT_MAGIC_64 0xbfbafecau
#define EMU_MACHO_CPU_TYPE_ARM64 0x0100000cu
#define EMU_MACHO_FILETYPE_EXECUTE 2u
#define EMU_MACHO_LC_SEGMENT_64 0x19u
#define EMU_MACHO_LC_LOAD_DYLIB 0x0cu
#define EMU_MACHO_LC_LOAD_DYLINKER 0x0eu
#define EMU_MACHO_LC_DYLD_INFO 0x22u
#define EMU_MACHO_LC_MAIN 0x80000028u
#define EMU_MACHO_LC_DYLD_INFO_ONLY 0x80000022u
#define EMU_MACHO_LC_SYMTAB 0x02u
#define EMU_MACHO_LC_DYSYMTAB 0x0bu
#define EMU_MACHO_LC_CODE_SIGNATURE 0x1du

typedef enum {
    EMU_INST_NOP = 0,
    EMU_INST_HLT,
    EMU_INST_MOVN,
    EMU_INST_MOVZ,
    EMU_INST_MOVK,
    EMU_INST_ADD_IMM,
    EMU_INST_SUB_IMM,
    EMU_INST_ADD_REG,
    EMU_INST_SUB_REG,
    EMU_INST_AND_REG,
    EMU_INST_ORR_REG,
    EMU_INST_EOR_REG,
    EMU_INST_LSL_IMM,
    EMU_INST_LSR_IMM,
    EMU_INST_ASR_IMM,
    EMU_INST_MUL,
    EMU_INST_UDIV,
    EMU_INST_SDIV,
    EMU_INST_ADR,
    EMU_INST_ADRP,
    EMU_INST_B,
    EMU_INST_BL,
    EMU_INST_B_COND,
    EMU_INST_CBZ,
    EMU_INST_CBNZ,
    EMU_INST_CMP_IMM,
    EMU_INST_CMP_REG,
    EMU_INST_LDR,
    EMU_INST_STR,
    EMU_INST_LDUR,
    EMU_INST_STUR,
    EMU_INST_LDP,
    EMU_INST_STP,
    EMU_INST_RET,
    EMU_INST_SVC,
    EMU_INST_BRK,
    EMU_INST_ERET,
} EmuInstructionKind;

typedef enum {
    EMU_ADDR_UNSIGNED_OFFSET = 0,
    EMU_ADDR_UNSCALED,
    EMU_ADDR_PRE_INDEX,
    EMU_ADDR_POST_INDEX,
    EMU_ADDR_PAIR_OFFSET,
} EmuAddressMode;

typedef enum {
    EMU_COND_EQ = 0,
    EMU_COND_NE = 1,
    EMU_COND_CS = 2,
    EMU_COND_CC = 3,
    EMU_COND_MI = 4,
    EMU_COND_PL = 5,
    EMU_COND_VS = 6,
    EMU_COND_VC = 7,
    EMU_COND_HI = 8,
    EMU_COND_LS = 9,
    EMU_COND_GE = 10,
    EMU_COND_LT = 11,
    EMU_COND_GT = 12,
    EMU_COND_LE = 13,
    EMU_COND_AL = 14,
} EmuCondition;

typedef struct {
    EmuInstructionKind kind;
    bool is_64_bit;
    bool sets_flags;
    uint8_t rd;
    uint8_t rn;
    uint8_t rm;
    uint8_t rt2;
    uint8_t shift_type;
    uint8_t shift_amount;
    EmuCondition condition;
    EmuAddressMode address_mode;
    uint8_t access_size;
    uint64_t imm;
    int64_t offset;
} EmuDecodedInstruction;

typedef struct {
    bool n;
    bool z;
    bool c;
    bool v;
} EmuFlags;

typedef struct {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    EmuFlags flags;
    bool halted;
    uint64_t instructions_executed;
} Cpu;

typedef enum {
    EMU_EXCEPTION_NONE = 0x00u,
    EMU_EXCEPTION_INVALID_INSTRUCTION = 0x01u,
    EMU_EXCEPTION_BREAKPOINT_OR_TRAP = 0x02u,
    EMU_EXCEPTION_SVC_TRAP = 0x03u,
    EMU_EXCEPTION_FETCH_FAULT = 0x10u,
    EMU_EXCEPTION_READ_FAULT = 0x11u,
    EMU_EXCEPTION_WRITE_FAULT = 0x12u,
    EMU_EXCEPTION_EXEC_PERMISSION_FAULT = 0x13u,
    EMU_EXCEPTION_READ_PERMISSION_FAULT = 0x14u,
    EMU_EXCEPTION_WRITE_PERMISSION_FAULT = 0x15u,
    EMU_EXCEPTION_DEVICE_FAULT = 0x20u,
    EMU_EXCEPTION_DIVIDE_BY_ZERO = 0x30u,
    EMU_EXCEPTION_TIMER_INTERRUPT = 0x40u,
} EmuExceptionCause;

typedef struct {
    EmuExceptionCause cause;
    uint64_t fault_address;
    uint64_t interrupted_pc;
    uint64_t resume_pc;
    EmuFlags flags;
    uint32_t depth;
} EmuExceptionContext;

typedef struct {
    bool vector_configured;
    uint64_t vector_base;
    bool active;
    bool interrupts_enabled;
    bool pending_timer_interrupt;
    uint64_t timer_interval;
    uint64_t next_timer_deadline;
    EmuExceptionContext context;
} EmuExceptionController;

typedef struct {
    uint64_t start;
    uint64_t size;
    uint8_t permissions;
    char name[32];
} EmuMemoryMapping;

typedef enum {
    EMU_DEVICE_UART = 0,
    EMU_DEVICE_TIMER,
    EMU_DEVICE_RANDOM,
    EMU_DEVICE_EXCEPTION,
} EmuDeviceKind;

typedef struct {
    uint64_t start;
    uint64_t size;
    uint8_t permissions;
    EmuDeviceKind kind;
    char name[32];
} EmuDeviceRange;

typedef struct {
    EmuDeviceRange ranges[4];
    size_t range_count;
    uint64_t timer_ticks;
    uint32_t random_state;
    FILE *uart_output;
    EmuExceptionController *exceptions;
} EmuDeviceBus;

typedef struct {
    uint8_t *bytes;
    size_t size;
    EmuMemoryMapping mappings[EMU_MAX_MEMORY_MAPPINGS];
    size_t mapping_count;
    bool permissions_enabled;
    EmuDeviceBus devices;
} Memory;

typedef enum {
    EMU_MEMORY_FAULT_NONE = 0,
    EMU_MEMORY_FAULT_BOUNDS,
    EMU_MEMORY_FAULT_UNMAPPED,
    EMU_MEMORY_FAULT_READ_PERMISSION,
    EMU_MEMORY_FAULT_WRITE_PERMISSION,
    EMU_MEMORY_FAULT_EXEC_PERMISSION,
} EmuMemoryFaultKind;

typedef enum {
    EMU_PROGRAM_RAW = 0,
    EMU_PROGRAM_ELF64,
    EMU_PROGRAM_MACHO64,
} EmuProgramFormat;

typedef struct {
    char name[17];
    uint64_t vaddr;
    uint64_t file_offset;
    uint64_t mem_size;
    uint64_t file_size;
    uint32_t flags;
    uint32_t section_count;
} EmuLoadedSegment;

typedef struct {
    char name[EMU_MAX_MACHO_SYMBOL_NAME];
    uint64_t address;
} EmuMachoSymbol;

typedef struct {
    EmuProgramFormat format;
    uint64_t entry;
    uint64_t stack_pointer;
    size_t segment_count;
    EmuLoadedSegment segments[EMU_MAX_LOAD_SEGMENTS];
    uint32_t macho_load_command_count;
    uint32_t macho_symbol_count;
    uint32_t macho_indirect_symbol_count;
    uint32_t macho_recorded_symbol_count;
    EmuMachoSymbol macho_symbols[EMU_MAX_MACHO_SYMBOLS];
} EmuLoadedProgram;

typedef struct {
    Cpu cpu;
    Memory memory;
    uint64_t instruction_limit;
    bool trace_enabled;
    FILE *trace_stream;
    FILE *stdout_stream;
    FILE *stderr_stream;
    uint8_t guest_exit_code;
    bool guest_exited;
    EmuExceptionController exceptions;
} Emulator;

typedef struct {
    uint64_t address;
    bool enabled;
} DebugBreakpoint;

typedef struct {
    Emulator emu;
    const char *path;
    DebugBreakpoint breakpoints[EMU_MAX_BREAKPOINTS];
    size_t breakpoint_count;
    bool loaded;
    bool stopped_at_breakpoint;
    uint64_t stopped_breakpoint_address;
} Debugger;

bool memory_init(Memory *memory, size_t size, char *error, size_t error_size);
void memory_free(Memory *memory);
void memory_clear_mappings(Memory *memory);
bool memory_map_range(Memory *memory, uint64_t address, uint64_t length, uint8_t permissions, const char *name,
                      char *error, size_t error_size);
bool memory_map_stack(Memory *memory, uint64_t stack_top, uint64_t stack_size, char *error, size_t error_size);
bool memory_check_read(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size);
bool memory_check_write(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size);
bool memory_check_execute(const Memory *memory, uint64_t address, uint64_t length, char *error, size_t error_size);
bool memory_check_access(const Memory *memory, uint64_t address, uint64_t length, uint8_t required,
                         EmuMemoryFaultKind *fault_kind, char *error, size_t error_size);
bool memory_fetch32(const Memory *memory, uint64_t address, uint32_t *out, char *error, size_t error_size);
void memory_format_permissions(uint8_t permissions, char *out, size_t out_size);
void memory_print_mappings(const Memory *memory, FILE *stream);
const EmuMemoryMapping *memory_find_mapping(const Memory *memory, uint64_t address);
bool memory_find_stack_guard(const Memory *memory, uint64_t address, EmuMemoryMapping *out);
void memory_print_devices(const Memory *memory, FILE *stream);
const EmuDeviceRange *memory_find_device(const Memory *memory, uint64_t address);
void memory_reset_devices(Memory *memory);
void memory_set_uart_output(Memory *memory, FILE *stream);
bool memory_read8(const Memory *memory, uint64_t address, uint8_t *out, char *error, size_t error_size);
bool memory_write8(Memory *memory, uint64_t address, uint8_t value, char *error, size_t error_size);
bool memory_read16(const Memory *memory, uint64_t address, uint16_t *out, char *error, size_t error_size);
bool memory_write16(Memory *memory, uint64_t address, uint16_t value, char *error, size_t error_size);
bool memory_read32(const Memory *memory, uint64_t address, uint32_t *out, char *error, size_t error_size);
bool memory_write32(Memory *memory, uint64_t address, uint32_t value, char *error, size_t error_size);
bool memory_read64(const Memory *memory, uint64_t address, uint64_t *out, char *error, size_t error_size);
bool memory_write64(Memory *memory, uint64_t address, uint64_t value, char *error, size_t error_size);

void cpu_init(Cpu *cpu, uint64_t pc, uint64_t sp);
uint64_t cpu_read_register(const Cpu *cpu, uint8_t index);
void cpu_write_register(Cpu *cpu, uint8_t index, bool is_64_bit, uint64_t value);
bool cpu_fetch(const Cpu *cpu, const Memory *memory, uint32_t *opcode, char *error, size_t error_size);
bool cpu_decode(uint32_t opcode, EmuDecodedInstruction *instruction, char *error, size_t error_size);
EmuStatus cpu_step(Cpu *cpu, Memory *memory, char *error, size_t error_size);
bool cpu_condition_passed(EmuFlags flags, EmuCondition condition);
bool cpu_calculate_branch_target(uint64_t pc, int64_t offset, const Memory *memory, uint64_t *target, char *error,
                                 size_t error_size);
bool cpu_calculate_memory_access(const Cpu *cpu, const EmuDecodedInstruction *instruction, const Memory *memory,
                                 uint64_t *address, uint64_t *writeback_value, bool *has_writeback, char *error,
                                 size_t error_size);
bool cpu_format_instruction(uint32_t opcode, uint64_t address, char *out, size_t out_size);
void cpu_dump(const Cpu *cpu, FILE *stream);

/*
 * Legacy raw-only loader retained for v0.1-v0.7 behavior and tests.
 * New v0.8+ call sites should prefer emulator_load_program(), which
 * auto-detects supported ELF64 files and falls back to the raw loader for
 * non-ELF input.
 */
bool load_raw_binary(Memory *memory, const char *path, uint64_t load_address, char *error, size_t error_size);
bool emulator_load_program(Emulator *emu, const char *path, EmuLoadedProgram *program, char *error, size_t error_size);

bool emulator_init(Emulator *emu, char *error, size_t error_size);
void emulator_free(Emulator *emu);
EmuStatus emulator_handle_syscall(Emulator *emu, const EmuDecodedInstruction *instruction, char *error,
                                  size_t error_size);
bool emulator_configure_exception_vector(Emulator *emu, uint64_t vector_base, char *error, size_t error_size);
void emulator_clear_exception_vector(Emulator *emu);
bool emulator_exception_return(Emulator *emu, char *error, size_t error_size);
EmuStatus emulator_raise_exception(Emulator *emu, EmuExceptionCause cause, uint64_t fault_address,
                                   uint64_t interrupted_pc, uint64_t resume_pc, char *error, size_t error_size);
void emulator_set_interrupts_enabled(Emulator *emu, bool enabled);
bool emulator_queue_timer_interrupt(Emulator *emu);
void emulator_configure_timer_interrupt(Emulator *emu, uint64_t interval);
const EmuExceptionContext *emulator_get_exception_context(const Emulator *emu);
EmuStatus emulator_step(Emulator *emu, char *error, size_t error_size);
EmuStatus emulator_run(Emulator *emu, char *error, size_t error_size);

bool debugger_init(Debugger *debugger, const char *path, char *error, size_t error_size);
void debugger_free(Debugger *debugger);
bool debugger_reset(Debugger *debugger, char *error, size_t error_size);
bool debugger_add_breakpoint(Debugger *debugger, uint64_t address, char *error, size_t error_size);
bool debugger_delete_breakpoint(Debugger *debugger, uint64_t address_or_id, char *error, size_t error_size);
bool debugger_has_breakpoint(const Debugger *debugger, uint64_t address);
void debugger_list_breakpoints(const Debugger *debugger, FILE *stream);
EmuStatus debugger_step(Debugger *debugger, char *error, size_t error_size);
EmuStatus debugger_continue(Debugger *debugger, char *error, size_t error_size);
int debugger_repl(Debugger *debugger, FILE *input, FILE *output, FILE *error_stream);

#endif
