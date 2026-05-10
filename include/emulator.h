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

typedef enum {
    EMU_INST_NOP = 0,
    EMU_INST_HLT,
    EMU_INST_MOVZ,
    EMU_INST_ADD_IMM,
    EMU_INST_SUB_IMM,
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
    uint8_t rd;
    uint8_t rn;
    uint8_t rm;
    uint8_t rt2;
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

typedef struct {
    uint8_t *bytes;
    size_t size;
} Memory;

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
bool memory_read8(const Memory *memory, uint64_t address, uint8_t *out, char *error, size_t error_size);
bool memory_write8(Memory *memory, uint64_t address, uint8_t value, char *error, size_t error_size);
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

bool load_raw_binary(Memory *memory, const char *path, uint64_t load_address, char *error, size_t error_size);

bool emulator_init(Emulator *emu, char *error, size_t error_size);
void emulator_free(Emulator *emu);
EmuStatus emulator_handle_syscall(Emulator *emu, const EmuDecodedInstruction *instruction, char *error,
                                  size_t error_size);
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
