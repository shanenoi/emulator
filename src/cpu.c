#include "emulator.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static int64_t sign_extend(uint64_t value, unsigned bits) {
    uint64_t sign_bit = 1ull << (bits - 1u);
    uint64_t mask = (1ull << bits) - 1ull;
    value &= mask;
    return (int64_t)((value ^ sign_bit) - sign_bit);
}

static bool checked_add_signed(uint64_t base, int64_t offset, uint64_t *result) {
    if (offset < 0) {
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1ull;
        if (magnitude > base) {
            return false;
        }
        *result = base - magnitude;
        return true;
    }

    uint64_t magnitude = (uint64_t)offset;
    if (magnitude > UINT64_MAX - base) {
        return false;
    }
    *result = base + magnitude;
    return true;
}

static bool check_data_range(const Memory *memory, uint64_t address, uint8_t width, char *error, size_t error_size) {
    if (address > (uint64_t)memory->size || width > memory->size ||
        address + width > (uint64_t)memory->size || address + width < address) {
        snprintf(error, error_size, "memory access out of bounds: address=0x%016" PRIx64 " width=%u memory_size=0x%zx",
                 address, (unsigned)width, memory->size);
        return false;
    }
    return true;
}

static uint64_t cpu_read_base_register(const Cpu *cpu, uint8_t index) {
    return index == 31 ? cpu->sp : cpu_read_register(cpu, index);
}

static void cpu_write_base_register(Cpu *cpu, uint8_t index, uint64_t value) {
    if (index == 31) {
        cpu->sp = value;
        return;
    }
    cpu_write_register(cpu, index, true, value);
}

bool cpu_calculate_branch_target(uint64_t pc, int64_t offset, const Memory *memory, uint64_t *target, char *error,
                                 size_t error_size) {
    uint64_t result = 0;

    if (offset < 0) {
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1ull;
        if (magnitude > pc) {
            snprintf(error, error_size, "branch target before memory: pc=0x%016" PRIx64 " offset=%" PRId64, pc,
                     offset);
            return false;
        }
        result = pc - magnitude;
    } else {
        uint64_t magnitude = (uint64_t)offset;
        if (magnitude > UINT64_MAX - pc) {
            snprintf(error, error_size, "branch target overflow: pc=0x%016" PRIx64 " offset=%" PRId64, pc,
                     offset);
            return false;
        }
        result = pc + magnitude;
    }

    if ((result & 0x3ull) != 0) {
        snprintf(error, error_size, "misaligned branch target: target=0x%016" PRIx64, result);
        return false;
    }

    if (result > (uint64_t)memory->size || memory->size - (size_t)result < sizeof(uint32_t)) {
        snprintf(error, error_size, "branch target outside memory: target=0x%016" PRIx64, result);
        return false;
    }

    *target = result;
    return true;
}

static void set_sub_flags(Cpu *cpu, uint64_t left, uint64_t right, bool is_64_bit) {
    uint64_t mask = is_64_bit ? UINT64_MAX : 0xffffffffull;
    uint64_t sign_bit = is_64_bit ? (1ull << 63u) : (1ull << 31u);
    uint64_t lhs = left & mask;
    uint64_t rhs = right & mask;
    uint64_t result = (lhs - rhs) & mask;

    cpu->flags.n = (result & sign_bit) != 0;
    cpu->flags.z = result == 0;
    cpu->flags.c = lhs >= rhs;
    cpu->flags.v = ((lhs ^ rhs) & (lhs ^ result) & sign_bit) != 0;
}

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

bool cpu_decode(uint32_t opcode, EmuDecodedInstruction *instruction, char *error, size_t error_size) {
    memset(instruction, 0, sizeof(*instruction));

    if (opcode == 0xd503201fu) {
        instruction->kind = EMU_INST_NOP;
        return true;
    }

    if ((opcode & 0xffe0001fu) == 0xd4400000u) {
        instruction->kind = EMU_INST_HLT;
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

        instruction->kind = EMU_INST_MOVZ;
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

        instruction->kind = is_sub ? EMU_INST_SUB_IMM : EMU_INST_ADD_IMM;
        instruction->is_64_bit = is_64_bit;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->imm = imm << (shift == 1 ? 12u : 0u);
        return true;
    }

    if ((opcode & 0x7c000000u) == 0x14000000u) {
        uint32_t imm26 = opcode & 0x03ffffffu;

        instruction->kind = EMU_INST_B;
        instruction->offset = sign_extend(imm26, 26u) * 4;
        return true;
    }

    if ((opcode & 0xff000010u) == 0x54000000u) {
        uint8_t condition = (uint8_t)(opcode & 0xfu);
        uint32_t imm19 = (opcode >> 5u) & 0x7ffffu;

        if (condition == 0xfu) {
            snprintf(error, error_size, "unsupported B.cond condition: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = EMU_INST_B_COND;
        instruction->condition = (EmuCondition)condition;
        instruction->offset = sign_extend(imm19, 19u) * 4;
        return true;
    }

    if ((opcode & 0x7e000000u) == 0x34000000u) {
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        bool is_cbnz = ((opcode >> 24u) & 0x1u) != 0;
        uint32_t imm19 = (opcode >> 5u) & 0x7ffffu;

        instruction->kind = is_cbnz ? EMU_INST_CBNZ : EMU_INST_CBZ;
        instruction->is_64_bit = is_64_bit;
        instruction->rn = (uint8_t)(opcode & 0x1fu);
        instruction->offset = sign_extend(imm19, 19u) * 4;
        return true;
    }

    if ((opcode & 0x1f000000u) == 0x11000000u && ((opcode >> 30u) & 0x1u) == 1u &&
        ((opcode >> 29u) & 0x1u) == 1u && (opcode & 0x1fu) == 0x1fu) {
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        uint8_t shift = (uint8_t)((opcode >> 22u) & 0x3u);
        uint64_t imm = (opcode >> 10u) & 0xfffu;

        if (shift > 1) {
            snprintf(error, error_size, "unsupported CMP immediate shift: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = EMU_INST_CMP_IMM;
        instruction->is_64_bit = is_64_bit;
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->imm = imm << (shift == 1 ? 12u : 0u);
        return true;
    }

    if ((opcode & 0x1f000000u) == 0x0b000000u && ((opcode >> 30u) & 0x1u) == 1u &&
        ((opcode >> 29u) & 0x1u) == 1u && (opcode & 0x1fu) == 0x1fu) {
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        uint8_t shift = (uint8_t)((opcode >> 22u) & 0x3u);
        uint8_t imm6 = (uint8_t)((opcode >> 10u) & 0x3fu);

        if (shift != 0 || imm6 != 0) {
            snprintf(error, error_size, "unsupported shifted CMP register: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = EMU_INST_CMP_REG;
        instruction->is_64_bit = is_64_bit;
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->rm = (uint8_t)((opcode >> 16u) & 0x1fu);
        return true;
    }

    if ((opcode & 0x3b000000u) == 0x39000000u) {
        uint8_t size = (uint8_t)((opcode >> 30u) & 0x3u);
        uint8_t opc = (uint8_t)((opcode >> 22u) & 0x3u);

        if ((size != 2 && size != 3) || (opc != 0 && opc != 1)) {
            snprintf(error, error_size, "unsupported LDR/STR unsigned-offset variant: opcode=0x%08x", opcode);
            return false;
        }

        uint8_t access_size = (uint8_t)(1u << size);
        uint64_t imm12 = (opcode >> 10u) & 0xfffu;

        instruction->kind = opc == 1 ? EMU_INST_LDR : EMU_INST_STR;
        instruction->is_64_bit = size == 3;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->address_mode = EMU_ADDR_UNSIGNED_OFFSET;
        instruction->access_size = access_size;
        instruction->offset = (int64_t)(imm12 * access_size);
        return true;
    }

    if ((opcode & 0x3b200000u) == 0x38000000u) {
        uint8_t size = (uint8_t)((opcode >> 30u) & 0x3u);
        uint8_t opc = (uint8_t)((opcode >> 22u) & 0x3u);
        uint8_t mode = (uint8_t)((opcode >> 10u) & 0x3u);

        if ((size != 2 && size != 3) || (opc != 0 && opc != 1) || mode == 2) {
            snprintf(error, error_size, "unsupported LDR/STR unscaled/write-back variant: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = opc == 1 ? EMU_INST_LDUR : EMU_INST_STUR;
        if (mode == 1) {
            instruction->kind = opc == 1 ? EMU_INST_LDR : EMU_INST_STR;
            instruction->address_mode = EMU_ADDR_POST_INDEX;
        } else if (mode == 3) {
            instruction->kind = opc == 1 ? EMU_INST_LDR : EMU_INST_STR;
            instruction->address_mode = EMU_ADDR_PRE_INDEX;
        } else {
            instruction->address_mode = EMU_ADDR_UNSCALED;
        }
        instruction->is_64_bit = size == 3;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->access_size = (uint8_t)(1u << size);
        instruction->offset = sign_extend((opcode >> 12u) & 0x1ffu, 9u);
        return true;
    }

    if ((opcode & 0x7e000000u) == 0x28000000u) {
        uint8_t size = (uint8_t)((opcode >> 30u) & 0x3u);
        uint8_t mode = (uint8_t)((opcode >> 23u) & 0x3u);
        bool is_load = ((opcode >> 22u) & 0x1u) != 0;

        if (size != 2 || mode == 0) {
            snprintf(error, error_size, "unsupported LDP/STP variant: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = is_load ? EMU_INST_LDP : EMU_INST_STP;
        instruction->is_64_bit = true;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rt2 = (uint8_t)((opcode >> 10u) & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->access_size = 8;
        instruction->offset = sign_extend((opcode >> 15u) & 0x7fu, 7u) * 8;
        if (mode == 1) {
            instruction->address_mode = EMU_ADDR_POST_INDEX;
        } else if (mode == 2) {
            instruction->address_mode = EMU_ADDR_PAIR_OFFSET;
        } else {
            instruction->address_mode = EMU_ADDR_PRE_INDEX;
        }
        return true;
    }

    snprintf(error, error_size, "unsupported instruction: opcode=0x%08x", opcode);
    return false;
}

bool cpu_condition_passed(EmuFlags flags, EmuCondition condition) {
    switch (condition) {
    case EMU_COND_EQ:
        return flags.z;
    case EMU_COND_NE:
        return !flags.z;
    case EMU_COND_CS:
        return flags.c;
    case EMU_COND_CC:
        return !flags.c;
    case EMU_COND_MI:
        return flags.n;
    case EMU_COND_PL:
        return !flags.n;
    case EMU_COND_VS:
        return flags.v;
    case EMU_COND_VC:
        return !flags.v;
    case EMU_COND_HI:
        return flags.c && !flags.z;
    case EMU_COND_LS:
        return !flags.c || flags.z;
    case EMU_COND_GE:
        return flags.n == flags.v;
    case EMU_COND_LT:
        return flags.n != flags.v;
    case EMU_COND_GT:
        return !flags.z && flags.n == flags.v;
    case EMU_COND_LE:
        return flags.z || flags.n != flags.v;
    case EMU_COND_AL:
        return true;
    }

    return false;
}

static bool calculate_memory_access(const Cpu *cpu, const EmuDecodedInstruction *instruction, const Memory *memory,
                                    uint64_t *address, uint64_t *writeback_value, bool *has_writeback, char *error,
                                    size_t error_size) {
    uint64_t base = cpu_read_base_register(cpu, instruction->rn);
    uint64_t access_address = base;
    uint64_t updated_base = base;
    bool writeback = false;

    switch (instruction->address_mode) {
    case EMU_ADDR_UNSIGNED_OFFSET:
    case EMU_ADDR_UNSCALED:
    case EMU_ADDR_PAIR_OFFSET:
        if (!checked_add_signed(base, instruction->offset, &access_address)) {
            snprintf(error, error_size, "memory address overflow: base=0x%016" PRIx64 " offset=%" PRId64, base,
                     instruction->offset);
            return false;
        }
        break;

    case EMU_ADDR_PRE_INDEX:
        if (!checked_add_signed(base, instruction->offset, &updated_base)) {
            snprintf(error, error_size, "pre-index write-back overflow: base=0x%016" PRIx64 " offset=%" PRId64,
                     base, instruction->offset);
            return false;
        }
        access_address = updated_base;
        writeback = true;
        break;

    case EMU_ADDR_POST_INDEX:
        access_address = base;
        if (!checked_add_signed(base, instruction->offset, &updated_base)) {
            snprintf(error, error_size, "post-index write-back overflow: base=0x%016" PRIx64 " offset=%" PRId64,
                     base, instruction->offset);
            return false;
        }
        writeback = true;
        break;
    }

    if (!check_data_range(memory, access_address, instruction->access_size, error, error_size)) {
        return false;
    }

    *address = access_address;
    *writeback_value = updated_base;
    *has_writeback = writeback;
    return true;
}

EmuStatus cpu_step(Cpu *cpu, Memory *memory, char *error, size_t error_size) {
    uint32_t opcode = 0;
    EmuDecodedInstruction instruction;
    uint64_t current_pc = cpu->pc;

    if (!cpu_fetch(cpu, memory, &opcode, error, error_size)) {
        return EMU_ERROR;
    }

    if (!cpu_decode(opcode, &instruction, error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "decode error at pc=0x%016" PRIx64 ": %s", current_pc, detail);
        return EMU_ERROR;
    }

    switch (instruction.kind) {
    case EMU_INST_NOP:
        cpu->pc += 4;
        break;

    case EMU_INST_HLT:
        cpu->halted = true;
        break;

    case EMU_INST_MOVZ:
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, instruction.imm);
        cpu->pc += 4;
        break;

    case EMU_INST_ADD_IMM: {
        uint64_t value = cpu_read_register(cpu, instruction.rn) + instruction.imm;
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_SUB_IMM: {
        uint64_t value = cpu_read_register(cpu, instruction.rn) - instruction.imm;
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_B: {
        uint64_t target = 0;
        if (!cpu_calculate_branch_target(current_pc, instruction.offset, memory, &target, error, error_size)) {
            return EMU_ERROR;
        }
        cpu->pc = target;
        break;
    }

    case EMU_INST_B_COND: {
        if (cpu_condition_passed(cpu->flags, instruction.condition)) {
            uint64_t target = 0;
            if (!cpu_calculate_branch_target(current_pc, instruction.offset, memory, &target, error, error_size)) {
                return EMU_ERROR;
            }
            cpu->pc = target;
        } else {
            cpu->pc += 4;
        }
        break;
    }

    case EMU_INST_CBZ:
    case EMU_INST_CBNZ: {
        uint64_t value = cpu_read_register(cpu, instruction.rn);
        if (!instruction.is_64_bit) {
            value &= 0xffffffffull;
        }

        bool is_zero = value == 0;
        bool should_branch = instruction.kind == EMU_INST_CBZ ? is_zero : !is_zero;
        if (should_branch) {
            uint64_t target = 0;
            if (!cpu_calculate_branch_target(current_pc, instruction.offset, memory, &target, error, error_size)) {
                return EMU_ERROR;
            }
            cpu->pc = target;
        } else {
            cpu->pc += 4;
        }
        break;
    }

    case EMU_INST_CMP_IMM:
        set_sub_flags(cpu, cpu_read_register(cpu, instruction.rn), instruction.imm, instruction.is_64_bit);
        cpu->pc += 4;
        break;

    case EMU_INST_CMP_REG:
        set_sub_flags(cpu, cpu_read_register(cpu, instruction.rn), cpu_read_register(cpu, instruction.rm),
                      instruction.is_64_bit);
        cpu->pc += 4;
        break;

    case EMU_INST_LDR:
    case EMU_INST_LDUR: {
        uint64_t address = 0;
        uint64_t writeback_value = 0;
        bool has_writeback = false;
        uint64_t value64 = 0;
        uint32_t value32 = 0;

        if (!calculate_memory_access(cpu, &instruction, memory, &address, &writeback_value, &has_writeback, error,
                                     error_size)) {
            return EMU_ERROR;
        }

        if (instruction.access_size == 8) {
            if (!memory_read64(memory, address, &value64, error, error_size)) {
                return EMU_ERROR;
            }
            cpu_write_register(cpu, instruction.rd, true, value64);
        } else {
            if (!memory_read32(memory, address, &value32, error, error_size)) {
                return EMU_ERROR;
            }
            cpu_write_register(cpu, instruction.rd, false, value32);
        }

        if (has_writeback) {
            cpu_write_base_register(cpu, instruction.rn, writeback_value);
        }
        cpu->pc += 4;
        break;
    }

    case EMU_INST_STR:
    case EMU_INST_STUR: {
        uint64_t address = 0;
        uint64_t writeback_value = 0;
        bool has_writeback = false;

        if (!calculate_memory_access(cpu, &instruction, memory, &address, &writeback_value, &has_writeback, error,
                                     error_size)) {
            return EMU_ERROR;
        }

        if (instruction.access_size == 8) {
            if (!memory_write64(memory, address, cpu_read_register(cpu, instruction.rd), error, error_size)) {
                return EMU_ERROR;
            }
        } else {
            if (!memory_write32(memory, address, (uint32_t)cpu_read_register(cpu, instruction.rd), error,
                                error_size)) {
                return EMU_ERROR;
            }
        }

        if (has_writeback) {
            cpu_write_base_register(cpu, instruction.rn, writeback_value);
        }
        cpu->pc += 4;
        break;
    }

    case EMU_INST_LDP: {
        uint64_t address = 0;
        uint64_t writeback_value = 0;
        bool has_writeback = false;
        uint64_t first = 0;
        uint64_t second = 0;

        if (!calculate_memory_access(cpu, &instruction, memory, &address, &writeback_value, &has_writeback, error,
                                     error_size)) {
            return EMU_ERROR;
        }
        if (!check_data_range(memory, address, 16, error, error_size)) {
            return EMU_ERROR;
        }
        if (!memory_read64(memory, address, &first, error, error_size) ||
            !memory_read64(memory, address + 8u, &second, error, error_size)) {
            return EMU_ERROR;
        }

        cpu_write_register(cpu, instruction.rd, true, first);
        cpu_write_register(cpu, instruction.rt2, true, second);
        if (has_writeback) {
            cpu_write_base_register(cpu, instruction.rn, writeback_value);
        }
        cpu->pc += 4;
        break;
    }

    case EMU_INST_STP: {
        uint64_t address = 0;
        uint64_t writeback_value = 0;
        bool has_writeback = false;

        if (!calculate_memory_access(cpu, &instruction, memory, &address, &writeback_value, &has_writeback, error,
                                     error_size)) {
            return EMU_ERROR;
        }
        if (!check_data_range(memory, address, 16, error, error_size)) {
            return EMU_ERROR;
        }

        if (!memory_write64(memory, address, cpu_read_register(cpu, instruction.rd), error, error_size) ||
            !memory_write64(memory, address + 8u, cpu_read_register(cpu, instruction.rt2), error, error_size)) {
            return EMU_ERROR;
        }

        if (has_writeback) {
            cpu_write_base_register(cpu, instruction.rn, writeback_value);
        }
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