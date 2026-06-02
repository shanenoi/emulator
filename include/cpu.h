#ifndef EMU_CPU_H
#define EMU_CPU_H

#include "emu_constants.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct Memory Memory;

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
    EMU_INST_ADD_EXT_REG,
    EMU_INST_SUB_EXT_REG,
    EMU_INST_AND_REG,
    EMU_INST_ORR_REG,
    EMU_INST_EOR_REG,
    EMU_INST_AND_IMM,
    EMU_INST_ORR_IMM,
    EMU_INST_EOR_IMM,
    EMU_INST_BITFIELD_UNSIGNED,
    EMU_INST_BITFIELD_SIGNED,
    EMU_INST_BITFIELD_INSERT,
    EMU_INST_LSL_IMM,
    EMU_INST_LSR_IMM,
    EMU_INST_ASR_IMM,
    EMU_INST_MUL,
    EMU_INST_MADD,
    EMU_INST_MSUB,
    EMU_INST_SMADDL,
    EMU_INST_SMSUBL,
    EMU_INST_UMADDL,
    EMU_INST_UMSUBL,
    EMU_INST_UDIV,
    EMU_INST_SDIV,
    EMU_INST_ADR,
    EMU_INST_ADRP,
    EMU_INST_B,
    EMU_INST_BL,
    EMU_INST_BR,
    EMU_INST_BLR,
    EMU_INST_B_COND,
    EMU_INST_CBZ,
    EMU_INST_CBNZ,
    EMU_INST_TBZ,
    EMU_INST_TBNZ,
    EMU_INST_CSEL,
    EMU_INST_CMP_IMM,
    EMU_INST_CMP_REG,
    EMU_INST_LDR,
    EMU_INST_LDR_LITERAL,
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
    EMU_ADDR_REGISTER_OFFSET,
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
    uint8_t extend_type;
    uint8_t bitfield_lsb;
    uint8_t bitfield_width;
    EmuCondition condition;
    EmuAddressMode address_mode;
    uint8_t access_size;
    bool sign_extend;
    uint64_t imm;
    int64_t offset;
} EmuDecodedInstruction;

typedef struct {
    bool n;
    bool z;
    bool c;
    bool v;
} EmuFlags;

typedef struct Cpu {
    uint64_t x[31];
    uint64_t sp;
    uint64_t pc;
    EmuFlags flags;
    bool halted;
    uint64_t instructions_executed;
} Cpu;

void cpu_init(Cpu *cpu, uint64_t pc, uint64_t sp);
uint64_t cpu_read_register(const Cpu *cpu, uint8_t index);
void cpu_write_register(Cpu *cpu, uint8_t index, bool is_64_bit, uint64_t value);
bool cpu_fetch(const Cpu *cpu, const Memory *memory, uint32_t *opcode, char *error, size_t error_size);
bool cpu_decode(uint32_t opcode, EmuDecodedInstruction *instruction, char *error, size_t error_size);
bool cpu_fetch_decode(Cpu *cpu, const Memory *memory, uint32_t *opcode, EmuDecodedInstruction *instruction,
                      char *error, size_t error_size);
EmuStatus cpu_execute_decoded(Cpu *cpu, Memory *memory, uint32_t opcode, const EmuDecodedInstruction *instruction,
                              char *error, size_t error_size);
EmuStatus cpu_step(Cpu *cpu, Memory *memory, char *error, size_t error_size);
bool cpu_condition_passed(EmuFlags flags, EmuCondition condition);
bool cpu_calculate_branch_target(uint64_t pc, int64_t offset, const Memory *memory, uint64_t *target, char *error,
                                 size_t error_size);
bool cpu_calculate_memory_access(const Cpu *cpu, const EmuDecodedInstruction *instruction, const Memory *memory,
                                 uint64_t *address, uint64_t *writeback_value, bool *has_writeback, char *error,
                                 size_t error_size);
bool cpu_format_instruction(uint32_t opcode, uint64_t address, char *out, size_t out_size);
void cpu_dump(const Cpu *cpu, FILE *stream);

#endif
