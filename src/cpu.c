#include "emulator.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

typedef enum {
    INST_NOP,
    INST_HLT,
    INST_MOVZ,
    INST_ADD_IMM,
    INST_SUB_IMM,
} InstructionKind;

typedef struct {
    InstructionKind kind;
    bool is_64_bit;
    uint8_t rd;
    uint8_t rn;
    uint64_t imm;
} DecodedInstruction;

void cpu_init(Cpu *cpu, uint64_t pc, uint64_t sp) {
    memset(cpu, 0, sizeof(*cpu));
    cpu->pc = pc;
    cpu->sp = sp;
}

uint64_t cpu_read_register(const Cpu *cpu, uint8_t index) {
    if (index >= 31) {
        return 0;
    }
    return cpu->x[index];
}

void cpu_write_register(Cpu *cpu, uint8_t index, bool is_64_bit, uint64_t value) {
    if (index >= 31) {
        return;
    }

    if (!is_64_bit) {
        value &= 0xffffffffull;
    }

    cpu->x[index] = value;
}

bool cpu_fetch(const Cpu *cpu, const Memory *memory, uint32_t *opcode, char *error, size_t error_size) {
    if ((cpu->pc & 0x3ull) != 0) {
        snprintf(error, error_size, "misaligned pc: pc=0x%016" PRIx64, cpu->pc);
        return false;
    }

    if (!memory_read32(memory, cpu->pc, opcode, error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "failed to fetch instruction at pc=0x%016" PRIx64 ": %s", cpu->pc, detail);
        return false;
    }

    return true;
}

static bool decode(uint32_t opcode, DecodedInstruction *instruction, char *error, size_t error_size) {
    memset(instruction, 0, sizeof(*instruction));

    if (opcode == 0xd503201fu) {
        instruction->kind = INST_NOP;
        return true;
    }

    if ((opcode & 0xffe0001fu) == 0xd4400000u) {
        instruction->kind = INST_HLT;
        instruction->imm = (opcode >> 5u) & 0xffffu;
        return true;
    }

    if ((opcode & 0x7f800000u) == 0x52800000u) {
        uint8_t sf = (uint8_t)((opcode >> 31u) & 0x1u);
        uint8_t hw = (uint8_t)((opcode >> 21u) & 0x3u);
        uint16_t imm16 = (uint16_t)((opcode >> 5u) & 0xffffu);

        if (sf == 0 && hw > 1) {
            snprintf(error, error_size, "unsupported MOVZ shift for 32-bit form: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = INST_MOVZ;
        instruction->is_64_bit = sf != 0;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->imm = ((uint64_t)imm16) << (16u * hw);
        return true;
    }

    if ((opcode & 0x1f000000u) == 0x11000000u && ((opcode >> 29u) & 0x1u) == 0) {
        bool is_sub = ((opcode >> 30u) & 0x1u) != 0;
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        uint8_t shift = (uint8_t)((opcode >> 22u) & 0x3u);
        uint64_t imm = (opcode >> 10u) & 0xfffu;

        if (shift > 1) {
            snprintf(error, error_size, "unsupported ADD/SUB immediate shift: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = is_sub ? INST_SUB_IMM : INST_ADD_IMM;
        instruction->is_64_bit = is_64_bit;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->imm = imm << (shift == 1 ? 12u : 0u);
        return true;
    }

    snprintf(error, error_size, "unsupported instruction: opcode=0x%08x", opcode);
    return false;
}

EmuStatus cpu_step(Cpu *cpu, const Memory *memory, char *error, size_t error_size) {
    uint32_t opcode = 0;
    DecodedInstruction instruction;
    uint64_t current_pc = cpu->pc;

    if (!cpu_fetch(cpu, memory, &opcode, error, error_size)) {
        return EMU_ERROR;
    }

    if (!decode(opcode, &instruction, error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "decode error at pc=0x%016" PRIx64 ": %s", current_pc, detail);
        return EMU_ERROR;
    }

    switch (instruction.kind) {
    case INST_NOP:
        cpu->pc += 4;
        break;

    case INST_HLT:
        cpu->halted = true;
        break;

    case INST_MOVZ:
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, instruction.imm);
        cpu->pc += 4;
        break;

    case INST_ADD_IMM: {
        uint64_t value = cpu_read_register(cpu, instruction.rn) + instruction.imm;
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case INST_SUB_IMM: {
        uint64_t value = cpu_read_register(cpu, instruction.rn) - instruction.imm;
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }
    }

    cpu->instructions_executed++;
    return cpu->halted ? EMU_HALTED : EMU_OK;
}

void cpu_dump(const Cpu *cpu, FILE *stream) {
    for (size_t i = 0; i < 31; i++) {
        fprintf(stream, "x%-2zu = 0x%016" PRIx64 "\n", i, cpu->x[i]);
    }
    fprintf(stream, "sp  = 0x%016" PRIx64 "\n", cpu->sp);
    fprintf(stream, "pc  = 0x%016" PRIx64 "\n", cpu->pc);
    fprintf(stream, "nzcv = %u%u%u%u\n", cpu->flags.n ? 1u : 0u, cpu->flags.z ? 1u : 0u,
            cpu->flags.c ? 1u : 0u, cpu->flags.v ? 1u : 0u);
    fprintf(stream, "instructions = 0x%016" PRIx64 "\n", cpu->instructions_executed);
}