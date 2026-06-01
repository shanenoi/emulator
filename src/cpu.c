#include "emulator.h"

#include "emu_util.h"

#include <inttypes.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

static int64_t sign_extend(uint64_t value, unsigned bits) {
    uint64_t sign_bit = 1ull << (bits - 1u);
    uint64_t mask = (1ull << bits) - 1ull;
    value &= mask;
    return (int64_t)((value ^ sign_bit) - sign_bit);
}

static bool check_data_range(const Memory *memory, uint64_t address, uint8_t width, char *error, size_t error_size) {
    uint64_t end = 0;
    if (!emu_checked_add_u64(address, width, &end) || address > (uint64_t)memory->size || width > memory->size ||
        end > (uint64_t)memory->size) {
        snprintf(error, error_size, "memory access out of bounds: address=0x%016" PRIx64 " width=%u memory_size=0x%zx",
                 address, (unsigned)width, memory->size);
        return false;
    }
    return true;
}

static uint64_t mask_for_width(bool is_64_bit) {
    return is_64_bit ? UINT64_MAX : 0xffffffffull;
}

static unsigned bits_for_width(bool is_64_bit) {
    return is_64_bit ? 64u : 32u;
}

static uint64_t read_gp_width(const Cpu *cpu, uint8_t index, bool is_64_bit) {
    uint64_t value = cpu_read_register(cpu, index);
    return is_64_bit ? value : (value & 0xffffffffull);
}

static uint64_t apply_shift(uint64_t value, uint8_t shift_type, uint8_t amount, bool is_64_bit) {
    value &= mask_for_width(is_64_bit);
    if (amount == 0) {
        return value;
    }

    switch (shift_type) {
    case 0: /* LSL */
        return (value << amount) & mask_for_width(is_64_bit);
    case 1: /* LSR */
        return value >> amount;
    case 2: { /* ASR */
        if (is_64_bit) {
            return (uint64_t)(((int64_t)value) >> amount);
        }
        return (uint32_t)(((int32_t)(uint32_t)value) >> amount);
    }
    default:
        return value;
    }
}

static uint64_t ones_mask(unsigned bits) {
    if (bits == 0) {
        return 0;
    }
    if (bits >= 64) {
        return UINT64_MAX;
    }
    return (1ull << bits) - 1ull;
}

static uint64_t rotate_right_width(uint64_t value, unsigned amount, unsigned width) {
    uint64_t mask = ones_mask(width);
    value &= mask;
    amount %= width;
    if (amount == 0) {
        return value;
    }
    return ((value >> amount) | (value << (width - amount))) & mask;
}

static int64_t sign_extend_width(uint64_t value, unsigned bits) {
    if (bits >= 64) {
        return (int64_t)value;
    }
    return sign_extend(value, bits);
}

static unsigned highest_set_bit(uint32_t value) {
    for (int bit = 31; bit >= 0; bit--) {
        if ((value & (1u << (unsigned)bit)) != 0) {
            return (unsigned)bit;
        }
    }
    return UINT_MAX;
}

static bool decode_logical_immediate(uint8_t n, uint8_t immr, uint8_t imms, bool is_64_bit, uint64_t *out) {
    unsigned reg_size = is_64_bit ? 64u : 32u;
    unsigned len = highest_set_bit(((uint32_t)n << 6u) | ((~(uint32_t)imms) & 0x3fu));
    if (len == UINT_MAX || len < 1u) {
        return false;
    }

    unsigned levels = (1u << len) - 1u;
    if (!is_64_bit && n != 0) {
        return false;
    }
    if ((imms & levels) == levels) {
        return false;
    }

    unsigned s = imms & levels;
    unsigned r = immr & levels;
    unsigned element_size = 1u << len;
    uint64_t element = rotate_right_width(ones_mask(s + 1u), r, element_size);
    uint64_t value = 0;
    for (unsigned bit = 0; bit < reg_size; bit += element_size) {
        value |= element << bit;
    }
    *out = value & ones_mask(reg_size);
    return true;
}

static uint64_t extend_register_value(uint64_t value, uint8_t extend_type) {
    switch (extend_type & 0x7u) {
    case 0: /* UXTB */
        return value & 0xffu;
    case 1: /* UXTH */
        return value & 0xffffu;
    case 2: /* UXTW */
        return value & 0xffffffffull;
    case 3: /* UXTX / LSL */
        return value;
    case 4: /* SXTB */
        return (uint64_t)sign_extend(value, 8u);
    case 5: /* SXTH */
        return (uint64_t)sign_extend(value, 16u);
    case 6: /* SXTW */
        return (uint64_t)sign_extend(value, 32u);
    case 7: /* SXTX */
        return value;
    default:
        return value;
    }
}

static void set_logical_flags(Cpu *cpu, uint64_t result, bool is_64_bit) {
    uint64_t value = result & mask_for_width(is_64_bit);
    uint64_t sign_bit = is_64_bit ? (1ull << 63u) : (1ull << 31u);
    cpu->flags.n = (value & sign_bit) != 0;
    cpu->flags.z = value == 0;
    cpu->flags.c = false;
    cpu->flags.v = false;
}

static bool memory_read_width(const Memory *memory, uint64_t address, uint8_t access_size, uint64_t *out, char *error,
                              size_t error_size) {
    uint8_t value8 = 0;
    uint16_t value16 = 0;
    uint32_t value32 = 0;
    uint64_t value64 = 0;

    switch (access_size) {
    case 1:
        if (!memory_read8(memory, address, &value8, error, error_size)) {
            return false;
        }
        *out = value8;
        return true;
    case 2:
        if (!memory_read16(memory, address, &value16, error, error_size)) {
            return false;
        }
        *out = value16;
        return true;
    case 4:
        if (!memory_read32(memory, address, &value32, error, error_size)) {
            return false;
        }
        *out = value32;
        return true;
    case 8:
        if (!memory_read64(memory, address, &value64, error, error_size)) {
            return false;
        }
        *out = value64;
        return true;
    default:
        snprintf(error, error_size, "unsupported access size: %u", (unsigned)access_size);
        return false;
    }
}

static bool memory_write_width(Memory *memory, uint64_t address, uint8_t access_size, uint64_t value, char *error,
                               size_t error_size) {
    switch (access_size) {
    case 1:
        return memory_write8(memory, address, (uint8_t)value, error, error_size);
    case 2:
        return memory_write16(memory, address, (uint16_t)value, error, error_size);
    case 4:
        return memory_write32(memory, address, (uint32_t)value, error, error_size);
    case 8:
        return memory_write64(memory, address, value, error, error_size);
    default:
        snprintf(error, error_size, "unsupported access size: %u", (unsigned)access_size);
        return false;
    }
}

static void add_instruction_context(char *error, size_t error_size, uint64_t pc, uint32_t opcode) {
    char detail[256];
    snprintf(detail, sizeof(detail), "%s", error);
    snprintf(error, error_size, "execution error at pc=0x%016" PRIx64 " opcode=0x%08x: %s", pc, opcode, detail);
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

static bool addsub_imm_uses_sp(const EmuDecodedInstruction *instruction) {
    if (instruction->sets_flags) {
        return false;
    }
    if (instruction->rn == 31 || instruction->rd == 31) {
        return true;
    }
    return instruction->rd == 29 && instruction->rn == 31;
}

bool cpu_calculate_branch_target(uint64_t pc, int64_t offset, const Memory *memory, uint64_t *target, char *error,
                                 size_t error_size) {
    uint64_t result = 0;

    if (!emu_checked_add_i64(pc, offset, &result)) {
        if (offset < 0) {
            snprintf(error, error_size, "branch target before memory: pc=0x%016" PRIx64 " offset=%" PRId64, pc,
                     offset);
        } else {
            snprintf(error, error_size, "branch target overflow: pc=0x%016" PRIx64 " offset=%" PRId64, pc,
                     offset);
        }
        return false;
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

static bool validate_indirect_branch_target(uint64_t target, const Memory *memory, const char *name, char *error,
                                            size_t error_size) {
    if ((target & 0x3ull) != 0) {
        snprintf(error, error_size, "misaligned %s target: target=0x%016" PRIx64, name, target);
        return false;
    }
    if (target > (uint64_t)memory->size || memory->size - (size_t)target < sizeof(uint32_t)) {
        snprintf(error, error_size, "%s target outside memory: target=0x%016" PRIx64, name, target);
        return false;
    }
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

static void set_add_flags(Cpu *cpu, uint64_t left, uint64_t right, bool is_64_bit) {
    uint64_t mask = is_64_bit ? UINT64_MAX : 0xffffffffull;
    uint64_t sign_bit = is_64_bit ? (1ull << 63u) : (1ull << 31u);
    uint64_t lhs = left & mask;
    uint64_t rhs = right & mask;
    uint64_t result = (lhs + rhs) & mask;

    cpu->flags.n = (result & sign_bit) != 0;
    cpu->flags.z = result == 0;
    cpu->flags.c = result < lhs;
    cpu->flags.v = (~(lhs ^ rhs) & (lhs ^ result) & sign_bit) != 0;
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

    if (!memory_fetch32(memory, cpu->pc, opcode, error, error_size)) {
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

    if ((opcode & 0xffe0001fu) == 0xd4000001u) {
        instruction->kind = EMU_INST_SVC;
        instruction->imm = (opcode >> 5u) & 0xffffu;
        return true;
    }

    if ((opcode & 0xffe0001fu) == 0xd4200000u) {
        instruction->kind = EMU_INST_BRK;
        instruction->imm = (opcode >> 5u) & 0xffffu;
        return true;
    }

    if (opcode == 0xd69f03e0u) {
        instruction->kind = EMU_INST_ERET;
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

    if ((opcode & 0x7f800000u) == 0x12800000u) {
        uint8_t sf = (uint8_t)((opcode >> 31u) & 0x1u);
        uint8_t hw = (uint8_t)((opcode >> 21u) & 0x3u);
        uint16_t imm16 = (uint16_t)((opcode >> 5u) & 0xffffu);

        if (sf == 0 && hw > 1) {
            snprintf(error, error_size, "unsupported MOVN shift for 32-bit form: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = EMU_INST_MOVN;
        instruction->is_64_bit = sf != 0;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->imm = ((uint64_t)imm16) << (16u * hw);
        instruction->shift_amount = (uint8_t)(16u * hw);
        return true;
    }

    if ((opcode & 0x7f800000u) == 0x72800000u) {
        uint8_t sf = (uint8_t)((opcode >> 31u) & 0x1u);
        uint8_t hw = (uint8_t)((opcode >> 21u) & 0x3u);
        uint16_t imm16 = (uint16_t)((opcode >> 5u) & 0xffffu);

        if (sf == 0 && hw > 1) {
            snprintf(error, error_size, "unsupported MOVK shift for 32-bit form: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = EMU_INST_MOVK;
        instruction->is_64_bit = sf != 0;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->imm = ((uint64_t)imm16) << (16u * hw);
        instruction->shift_amount = (uint8_t)(16u * hw);
        return true;
    }

    if ((opcode & 0x1f000000u) == 0x11000000u && !(((opcode >> 29u) & 0x1u) == 1u &&
                                                      (opcode & 0x1fu) == 0x1fu)) {
        bool is_sub = ((opcode >> 30u) & 0x1u) != 0;
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        bool sets_flags = ((opcode >> 29u) & 0x1u) != 0;
        uint8_t shift = (uint8_t)((opcode >> 22u) & 0x3u);
        uint64_t imm = (opcode >> 10u) & 0xfffu;

        if (shift > 1) {
            snprintf(error, error_size, "unsupported ADD/SUB immediate shift: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = is_sub ? EMU_INST_SUB_IMM : EMU_INST_ADD_IMM;
        instruction->is_64_bit = is_64_bit;
        instruction->sets_flags = sets_flags;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->imm = imm << (shift == 1 ? 12u : 0u);
        return true;
    }

    if ((opcode & 0x1f200000u) == 0x0b200000u && !(((opcode >> 29u) & 0x1u) == 1u &&
                                                      (opcode & 0x1fu) == 0x1fu)) {
        bool is_sub = ((opcode >> 30u) & 0x1u) != 0;
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        bool sets_flags = ((opcode >> 29u) & 0x1u) != 0;
        uint8_t extend_type = (uint8_t)((opcode >> 13u) & 0x7u);
        uint8_t shift_amount = (uint8_t)((opcode >> 10u) & 0x7u);

        if (shift_amount > 4u) {
            snprintf(error, error_size, "unsupported ADD/SUB extended-register shift: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = is_sub ? EMU_INST_SUB_EXT_REG : EMU_INST_ADD_EXT_REG;
        instruction->is_64_bit = is_64_bit;
        instruction->sets_flags = sets_flags;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->rm = (uint8_t)((opcode >> 16u) & 0x1fu);
        instruction->extend_type = extend_type;
        instruction->shift_amount = shift_amount;
        return true;
    }

    if ((opcode & 0x1f000000u) == 0x0b000000u && !(((opcode >> 29u) & 0x1u) == 1u &&
                                                      (opcode & 0x1fu) == 0x1fu)) {
        bool is_sub = ((opcode >> 30u) & 0x1u) != 0;
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        bool sets_flags = ((opcode >> 29u) & 0x1u) != 0;
        uint8_t shift_type = (uint8_t)((opcode >> 22u) & 0x3u);
        uint8_t shift_amount = (uint8_t)((opcode >> 10u) & 0x3fu);

        if (shift_type == 3 || (!is_64_bit && shift_amount > 31)) {
            snprintf(error, error_size, "unsupported ADD/SUB shifted-register variant: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = is_sub ? EMU_INST_SUB_REG : EMU_INST_ADD_REG;
        instruction->is_64_bit = is_64_bit;
        instruction->sets_flags = sets_flags;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->rm = (uint8_t)((opcode >> 16u) & 0x1fu);
        instruction->shift_type = shift_type;
        instruction->shift_amount = shift_amount;
        return true;
    }

    if ((opcode & 0x1f800000u) == 0x12000000u) {
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        uint8_t opc = (uint8_t)((opcode >> 29u) & 0x3u);
        uint8_t n = (uint8_t)((opcode >> 22u) & 0x1u);
        uint8_t immr = (uint8_t)((opcode >> 16u) & 0x3fu);
        uint8_t imms = (uint8_t)((opcode >> 10u) & 0x3fu);
        uint64_t imm = 0;

        if (!decode_logical_immediate(n, immr, imms, is_64_bit, &imm)) {
            snprintf(error, error_size, "unsupported logical immediate encoding: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = opc == 0 ? EMU_INST_AND_IMM : (opc == 1 ? EMU_INST_ORR_IMM : EMU_INST_EOR_IMM);
        instruction->is_64_bit = is_64_bit;
        instruction->sets_flags = opc == 3;
        if (opc == 3) {
            instruction->kind = EMU_INST_AND_IMM;
        }
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->imm = imm;
        return true;
    }

    if ((opcode & 0x1f000000u) == 0x0a000000u) {
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        uint8_t opc = (uint8_t)((opcode >> 29u) & 0x3u);
        uint8_t shift_type = (uint8_t)((opcode >> 22u) & 0x3u);
        uint8_t shift_amount = (uint8_t)((opcode >> 10u) & 0x3fu);

        if (opc == 3 || shift_type == 3 || (!is_64_bit && shift_amount > 31)) {
            snprintf(error, error_size, "unsupported logical shifted-register variant: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = opc == 0 ? EMU_INST_AND_REG : (opc == 1 ? EMU_INST_ORR_REG : EMU_INST_EOR_REG);
        instruction->is_64_bit = is_64_bit;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->rm = (uint8_t)((opcode >> 16u) & 0x1fu);
        instruction->shift_type = shift_type;
        instruction->shift_amount = shift_amount;
        return true;
    }

    if ((opcode & 0x7f000000u) == 0x53000000u || (opcode & 0x7f000000u) == 0x33000000u ||
        (opcode & 0x7f000000u) == 0x13000000u) {
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        uint8_t opc = (uint8_t)((opcode >> 29u) & 0x3u);
        bool is_signed = opc == 0;
        bool is_insert = opc == 1;
        bool is_unsigned = opc == 2;
        bool n_bit = ((opcode >> 22u) & 0x1u) != 0;
        uint8_t immr = (uint8_t)((opcode >> 16u) & 0x3fu);
        uint8_t imms = (uint8_t)((opcode >> 10u) & 0x3fu);
        unsigned width = bits_for_width(is_64_bit);

        if (n_bit != is_64_bit || (!is_64_bit && (immr > 31 || imms > 31)) ||
            (!is_signed && !is_unsigned && !is_insert)) {
            snprintf(error, error_size, "unsupported bitfield/shift variant: opcode=0x%08x", opcode);
            return false;
        }

        instruction->is_64_bit = is_64_bit;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        if (is_unsigned && imms == width - 1u) {
            instruction->kind = EMU_INST_LSR_IMM;
            instruction->shift_amount = immr;
            return true;
        }
        if (is_signed && imms == width - 1u) {
            instruction->kind = EMU_INST_ASR_IMM;
            instruction->shift_amount = immr;
            return true;
        }
        if (is_unsigned && immr == ((imms + 1u) & (width - 1u))) {
            instruction->kind = EMU_INST_LSL_IMM;
            instruction->shift_amount = (uint8_t)((width - immr) & (width - 1u));
            return true;
        }

        instruction->kind = is_signed ? EMU_INST_BITFIELD_SIGNED
                                      : (is_insert ? EMU_INST_BITFIELD_INSERT : EMU_INST_BITFIELD_UNSIGNED);
        instruction->bitfield_lsb = immr;
        instruction->bitfield_width = imms;
        return true;
    }

    if ((opcode & 0x7fe0fc00u) == 0x1ac00800u || (opcode & 0x7fe0fc00u) == 0x1ac00c00u) {
        bool is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        bool is_signed = ((opcode & 0x7fe0fc00u) == 0x1ac00c00u);
        instruction->kind = is_signed ? EMU_INST_SDIV : EMU_INST_UDIV;
        instruction->is_64_bit = is_64_bit;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->rm = (uint8_t)((opcode >> 16u) & 0x1fu);
        return true;
    }

    if ((opcode & 0x7fe00000u) == 0x1b000000u) {
        bool is_sub = ((opcode >> 15u) & 0x1u) != 0;
        uint8_t ra = (uint8_t)((opcode >> 10u) & 0x1fu);

        instruction->kind = (!is_sub && ra == 31u) ? EMU_INST_MUL : (is_sub ? EMU_INST_MSUB : EMU_INST_MADD);
        instruction->is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->rm = (uint8_t)((opcode >> 16u) & 0x1fu);
        instruction->rt2 = ra;
        return true;
    }

    if ((opcode & 0xffe00000u) == 0x9b200000u || (opcode & 0xffe00000u) == 0x9ba00000u) {
        bool is_unsigned = ((opcode >> 23u) & 0x1u) != 0;
        bool is_sub = ((opcode >> 15u) & 0x1u) != 0;
        instruction->kind = is_unsigned ? (is_sub ? EMU_INST_UMSUBL : EMU_INST_UMADDL)
                                        : (is_sub ? EMU_INST_SMSUBL : EMU_INST_SMADDL);
        instruction->is_64_bit = true;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->rm = (uint8_t)((opcode >> 16u) & 0x1fu);
        instruction->rt2 = (uint8_t)((opcode >> 10u) & 0x1fu);
        return true;
    }

    if ((opcode & 0x9f000000u) == 0x10000000u || (opcode & 0x9f000000u) == 0x90000000u) {
        uint64_t immlo = (opcode >> 29u) & 0x3u;
        uint64_t immhi = (opcode >> 5u) & 0x7ffffu;
        int64_t signed_imm = sign_extend((immhi << 2u) | immlo, 21u);
        bool is_adrp = (opcode & 0x80000000u) != 0;

        instruction->kind = is_adrp ? EMU_INST_ADRP : EMU_INST_ADR;
        instruction->is_64_bit = true;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->offset = is_adrp ? signed_imm * 4096ll : signed_imm;
        return true;
    }

    if ((opcode & 0xfc000000u) == 0x94000000u) {
        uint32_t imm26 = opcode & 0x03ffffffu;

        instruction->kind = EMU_INST_BL;
        instruction->offset = sign_extend(imm26, 26u) * 4;
        return true;
    }

    if ((opcode & 0xfc000000u) == 0x14000000u) {
        uint32_t imm26 = opcode & 0x03ffffffu;

        instruction->kind = EMU_INST_B;
        instruction->offset = sign_extend(imm26, 26u) * 4;
        return true;
    }

    if ((opcode & 0xfffffc1fu) == 0xd61f0000u) {
        instruction->kind = EMU_INST_BR;
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        return true;
    }

    if ((opcode & 0xfffffc1fu) == 0xd63f0000u) {
        instruction->kind = EMU_INST_BLR;
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        return true;
    }

    if ((opcode & 0xfffffc1fu) == 0xd65f0000u) {
        instruction->kind = EMU_INST_RET;
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
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

    if ((opcode & 0x7e000000u) == 0x36000000u) {
        bool is_tbnz = ((opcode >> 24u) & 0x1u) != 0;
        uint8_t bit = (uint8_t)(((opcode >> 19u) & 0x1fu) | (((opcode >> 31u) & 0x1u) << 5u));
        uint32_t imm14 = (opcode >> 5u) & 0x3fffu;

        instruction->kind = is_tbnz ? EMU_INST_TBNZ : EMU_INST_TBZ;
        instruction->is_64_bit = bit >= 32u;
        instruction->rn = (uint8_t)(opcode & 0x1fu);
        instruction->shift_amount = bit;
        instruction->offset = sign_extend(imm14, 14u) * 4;
        return true;
    }

    if ((opcode & 0x1fe00000u) == 0x1a800000u) {
        instruction->kind = EMU_INST_CSEL;
        instruction->is_64_bit = ((opcode >> 31u) & 0x1u) != 0;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->rm = (uint8_t)((opcode >> 16u) & 0x1fu);
        instruction->condition = (EmuCondition)((opcode >> 12u) & 0xfu);
        instruction->shift_type = (uint8_t)((((opcode >> 30u) & 0x1u) << 1u) | ((opcode >> 10u) & 0x1u));
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

    if ((opcode & 0x3b000000u) == 0x18000000u) {
        uint8_t opc = (uint8_t)((opcode >> 30u) & 0x3u);
        uint32_t imm19 = (opcode >> 5u) & 0x7ffffu;

        if (opc == 3u) {
            snprintf(error, error_size, "unsupported literal prefetch variant: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = EMU_INST_LDR_LITERAL;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->offset = sign_extend(imm19, 19u) * 4;
        instruction->access_size = opc == 1u ? 8u : 4u;
        instruction->is_64_bit = opc != 0u;
        instruction->sign_extend = opc == 2u;
        return true;
    }

    if ((opcode & 0x3b000000u) == 0x39000000u) {
        uint8_t size = (uint8_t)((opcode >> 30u) & 0x3u);
        uint8_t opc = (uint8_t)((opcode >> 22u) & 0x3u);

        if (opc == 3u && size >= 2u) {
            snprintf(error, error_size, "unsupported LDR/STR unsigned-offset variant: opcode=0x%08x", opcode);
            return false;
        }

        uint8_t access_size = (uint8_t)(1u << size);
        uint64_t imm12 = (opcode >> 10u) & 0xfffu;

        instruction->kind = opc == 0 ? EMU_INST_STR : EMU_INST_LDR;
        instruction->is_64_bit = size == 3 || opc == 2u;
        instruction->sign_extend = opc >= 2u;
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

        if (mode == 2 || (opc == 3u && size >= 2u)) {
            snprintf(error, error_size, "unsupported LDR/STR unscaled/write-back variant: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = opc == 0 ? EMU_INST_STUR : EMU_INST_LDUR;
        if (mode == 1) {
            instruction->kind = opc == 0 ? EMU_INST_STR : EMU_INST_LDR;
            instruction->address_mode = EMU_ADDR_POST_INDEX;
        } else if (mode == 3) {
            instruction->kind = opc == 0 ? EMU_INST_STR : EMU_INST_LDR;
            instruction->address_mode = EMU_ADDR_PRE_INDEX;
        } else {
            instruction->address_mode = EMU_ADDR_UNSCALED;
        }
        instruction->is_64_bit = size == 3 || opc == 2u;
        instruction->sign_extend = opc >= 2u;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->access_size = (uint8_t)(1u << size);
        instruction->offset = sign_extend((opcode >> 12u) & 0x1ffu, 9u);
        return true;
    }

    if ((opcode & 0x3b200c00u) == 0x38200800u) {
        uint8_t size = (uint8_t)((opcode >> 30u) & 0x3u);
        uint8_t opc = (uint8_t)((opcode >> 22u) & 0x3u);
        uint8_t extend_type = (uint8_t)((opcode >> 13u) & 0x7u);
        bool scaled = ((opcode >> 12u) & 0x1u) != 0;

        if (opc == 3u && size >= 2u) {
            snprintf(error, error_size, "unsupported LDR/STR register-offset variant: opcode=0x%08x", opcode);
            return false;
        }

        instruction->kind = opc == 0 ? EMU_INST_STR : EMU_INST_LDR;
        instruction->is_64_bit = size == 3 || opc == 2u;
        instruction->sign_extend = opc >= 2u;
        instruction->rd = (uint8_t)(opcode & 0x1fu);
        instruction->rn = (uint8_t)((opcode >> 5u) & 0x1fu);
        instruction->rm = (uint8_t)((opcode >> 16u) & 0x1fu);
        instruction->address_mode = EMU_ADDR_REGISTER_OFFSET;
        instruction->access_size = (uint8_t)(1u << size);
        instruction->extend_type = extend_type;
        instruction->shift_amount = scaled ? size : 0u;
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

bool cpu_calculate_memory_access(const Cpu *cpu, const EmuDecodedInstruction *instruction, const Memory *memory,
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
        if (!emu_checked_add_i64(base, instruction->offset, &access_address)) {
            snprintf(error, error_size, "memory address overflow: base=0x%016" PRIx64 " offset=%" PRId64, base,
                     instruction->offset);
            return false;
        }
        break;

    case EMU_ADDR_REGISTER_OFFSET: {
        uint64_t offset = extend_register_value(cpu_read_register(cpu, instruction->rm), instruction->extend_type);
        if (instruction->shift_amount != 0) {
            offset <<= instruction->shift_amount;
        }
        if (!emu_checked_add_u64(base, offset, &access_address)) {
            snprintf(error, error_size, "register-offset memory address overflow: base=0x%016" PRIx64
                                          " offset=0x%016" PRIx64,
                     base, offset);
            return false;
        }
        break;
    }

    case EMU_ADDR_PRE_INDEX:
        if (!emu_checked_add_i64(base, instruction->offset, &updated_base)) {
            snprintf(error, error_size, "pre-index write-back overflow: base=0x%016" PRIx64 " offset=%" PRId64,
                     base, instruction->offset);
            return false;
        }
        access_address = updated_base;
        writeback = true;
        break;

    case EMU_ADDR_POST_INDEX:
        access_address = base;
        if (!emu_checked_add_i64(base, instruction->offset, &updated_base)) {
            snprintf(error, error_size, "post-index write-back overflow: base=0x%016" PRIx64 " offset=%" PRId64,
                     base, instruction->offset);
            return false;
        }
        writeback = true;
        break;
    }

    if (memory_find_device(memory, access_address) == NULL &&
        !check_data_range(memory, access_address, instruction->access_size, error, error_size)) {
        return false;
    }

    *address = access_address;
    *writeback_value = updated_base;
    *has_writeback = writeback;
    return true;
}

bool cpu_fetch_decode(Cpu *cpu, const Memory *memory, uint32_t *opcode_out, EmuDecodedInstruction *instruction,
                      char *error, size_t error_size) {
    uint32_t opcode = 0;
    uint64_t current_pc = cpu->pc;

    if (!cpu_fetch(cpu, memory, &opcode, error, error_size)) {
        return false;
    }

    if (!cpu_decode(opcode, instruction, error, error_size)) {
        char detail[256];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "decode error at pc=0x%016" PRIx64 ": %s", current_pc, detail);
        return false;
    }

    if (opcode_out != NULL) {
        *opcode_out = opcode;
    }

    return true;
}

EmuStatus cpu_execute_decoded(Cpu *cpu, Memory *memory, uint32_t opcode,
                              const EmuDecodedInstruction *decoded_instruction, char *error, size_t error_size) {
    EmuDecodedInstruction instruction = *decoded_instruction;
    uint64_t current_pc = cpu->pc;

    switch (instruction.kind) {
    case EMU_INST_NOP:
        cpu->pc += 4;
        break;

    case EMU_INST_HLT:
        cpu->halted = true;
        break;

    case EMU_INST_SVC:
        snprintf(error, error_size, "svc requires emulator syscall dispatcher");
        return EMU_ERROR;

    case EMU_INST_BRK:
        snprintf(error, error_size, "brk requires emulator exception dispatcher");
        return EMU_ERROR;

    case EMU_INST_ERET:
        snprintf(error, error_size, "eret requires emulator exception dispatcher");
        return EMU_ERROR;

    case EMU_INST_MOVZ:
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, instruction.imm);
        cpu->pc += 4;
        break;

    case EMU_INST_MOVN:
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, ~instruction.imm);
        cpu->pc += 4;
        break;

    case EMU_INST_MOVK: {
        uint64_t old_value = read_gp_width(cpu, instruction.rd, instruction.is_64_bit);
        uint64_t mask = 0xffffull << instruction.shift_amount;
        uint64_t value = (old_value & ~mask) | instruction.imm;
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_ADD_IMM: {
        bool uses_sp = addsub_imm_uses_sp(&instruction);
        uint64_t lhs = uses_sp ? cpu_read_base_register(cpu, instruction.rn) : cpu_read_register(cpu, instruction.rn);
        uint64_t value = lhs + instruction.imm;
        if (uses_sp && instruction.rd == 31) {
            cpu_write_base_register(cpu, instruction.rd, value);
        } else {
            cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        }
        if (instruction.sets_flags) {
            set_add_flags(cpu, lhs, instruction.imm, instruction.is_64_bit);
        }
        cpu->pc += 4;
        break;
    }

    case EMU_INST_SUB_IMM: {
        bool uses_sp = addsub_imm_uses_sp(&instruction);
        uint64_t lhs = uses_sp ? cpu_read_base_register(cpu, instruction.rn) : cpu_read_register(cpu, instruction.rn);
        uint64_t value = lhs - instruction.imm;
        if (uses_sp && instruction.rd == 31) {
            cpu_write_base_register(cpu, instruction.rd, value);
        } else {
            cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        }
        if (instruction.sets_flags) {
            set_sub_flags(cpu, lhs, instruction.imm, instruction.is_64_bit);
        }
        cpu->pc += 4;
        break;
    }

    case EMU_INST_ADD_REG:
    case EMU_INST_SUB_REG: {
        uint64_t lhs = read_gp_width(cpu, instruction.rn, instruction.is_64_bit);
        uint64_t rhs = apply_shift(read_gp_width(cpu, instruction.rm, instruction.is_64_bit), instruction.shift_type,
                                   instruction.shift_amount, instruction.is_64_bit);
        uint64_t value = instruction.kind == EMU_INST_ADD_REG ? lhs + rhs : lhs - rhs;
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        if (instruction.sets_flags) {
            if (instruction.kind == EMU_INST_ADD_REG) {
                set_add_flags(cpu, lhs, rhs, instruction.is_64_bit);
            } else {
                set_sub_flags(cpu, lhs, rhs, instruction.is_64_bit);
            }
        }
        cpu->pc += 4;
        break;
    }

    case EMU_INST_ADD_EXT_REG:
    case EMU_INST_SUB_EXT_REG: {
        bool uses_sp = !instruction.sets_flags;
        uint64_t lhs = uses_sp ? cpu_read_base_register(cpu, instruction.rn) : read_gp_width(cpu, instruction.rn, instruction.is_64_bit);
        uint64_t rhs = extend_register_value(cpu_read_register(cpu, instruction.rm), instruction.extend_type);
        if (instruction.shift_amount != 0) {
            rhs <<= instruction.shift_amount;
        }
        rhs &= mask_for_width(instruction.is_64_bit);
        uint64_t value = instruction.kind == EMU_INST_ADD_EXT_REG ? lhs + rhs : lhs - rhs;
        if (uses_sp && instruction.rd == 31) {
            cpu_write_base_register(cpu, instruction.rd, value);
        } else {
            cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        }
        if (instruction.sets_flags) {
            if (instruction.kind == EMU_INST_ADD_EXT_REG) {
                set_add_flags(cpu, lhs, rhs, instruction.is_64_bit);
            } else {
                set_sub_flags(cpu, lhs, rhs, instruction.is_64_bit);
            }
        }
        cpu->pc += 4;
        break;
    }

    case EMU_INST_AND_REG:
    case EMU_INST_ORR_REG:
    case EMU_INST_EOR_REG: {
        uint64_t lhs = read_gp_width(cpu, instruction.rn, instruction.is_64_bit);
        uint64_t rhs = apply_shift(read_gp_width(cpu, instruction.rm, instruction.is_64_bit), instruction.shift_type,
                                   instruction.shift_amount, instruction.is_64_bit);
        uint64_t value = 0;
        if (instruction.kind == EMU_INST_AND_REG) {
            value = lhs & rhs;
        } else if (instruction.kind == EMU_INST_ORR_REG) {
            value = lhs | rhs;
        } else {
            value = lhs ^ rhs;
        }
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_AND_IMM:
    case EMU_INST_ORR_IMM:
    case EMU_INST_EOR_IMM: {
        uint64_t lhs = read_gp_width(cpu, instruction.rn, instruction.is_64_bit);
        uint64_t value = 0;
        if (instruction.kind == EMU_INST_AND_IMM) {
            value = lhs & instruction.imm;
        } else if (instruction.kind == EMU_INST_ORR_IMM) {
            value = lhs | instruction.imm;
        } else {
            value = lhs ^ instruction.imm;
        }
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        if (instruction.sets_flags) {
            set_logical_flags(cpu, value, instruction.is_64_bit);
        }
        cpu->pc += 4;
        break;
    }

    case EMU_INST_BITFIELD_UNSIGNED:
    case EMU_INST_BITFIELD_SIGNED:
    case EMU_INST_BITFIELD_INSERT: {
        unsigned width = bits_for_width(instruction.is_64_bit);
        uint8_t immr = instruction.bitfield_lsb;
        uint8_t imms = instruction.bitfield_width;
        uint64_t src = read_gp_width(cpu, instruction.rn, instruction.is_64_bit);
        uint64_t result = 0;

        if (instruction.kind == EMU_INST_BITFIELD_INSERT) {
            uint64_t old_value = read_gp_width(cpu, instruction.rd, instruction.is_64_bit);
            if (imms >= immr) {
                unsigned field_width = (unsigned)(imms - immr + 1u);
                uint64_t mask = ones_mask(field_width);
                result = (old_value & ~mask) | ((src >> immr) & mask);
            } else {
                unsigned lsb = width - immr;
                unsigned field_width = (unsigned)imms + 1u;
                uint64_t mask = ones_mask(field_width) << lsb;
                result = (old_value & ~mask) | ((src << lsb) & mask);
            }
        } else {
            uint64_t extracted = 0;
            unsigned field_width = 0;
            if (imms >= immr) {
                field_width = (unsigned)(imms - immr + 1u);
                extracted = (src >> immr) & ones_mask(field_width);
            } else {
                field_width = (unsigned)imms + 1u;
                extracted = rotate_right_width(src, immr, width) & ones_mask(field_width);
            }
            if (instruction.kind == EMU_INST_BITFIELD_SIGNED) {
                result = (uint64_t)sign_extend_width(extracted, field_width);
            } else {
                result = extracted;
            }
        }

        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, result);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_LSL_IMM:
    case EMU_INST_LSR_IMM:
    case EMU_INST_ASR_IMM: {
        uint8_t shift_type = instruction.kind == EMU_INST_LSL_IMM ? 0u : (instruction.kind == EMU_INST_LSR_IMM ? 1u : 2u);
        uint64_t value = apply_shift(read_gp_width(cpu, instruction.rn, instruction.is_64_bit), shift_type,
                                     instruction.shift_amount, instruction.is_64_bit);
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_MUL: {
        uint64_t value = read_gp_width(cpu, instruction.rn, instruction.is_64_bit) *
                         read_gp_width(cpu, instruction.rm, instruction.is_64_bit);
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_MADD:
    case EMU_INST_MSUB: {
        uint64_t lhs = read_gp_width(cpu, instruction.rn, instruction.is_64_bit);
        uint64_t rhs = read_gp_width(cpu, instruction.rm, instruction.is_64_bit);
        uint64_t acc = read_gp_width(cpu, instruction.rt2, instruction.is_64_bit);
        uint64_t product = lhs * rhs;
        uint64_t value = instruction.kind == EMU_INST_MADD ? acc + product : acc - product;
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_SMADDL:
    case EMU_INST_SMSUBL:
    case EMU_INST_UMADDL:
    case EMU_INST_UMSUBL: {
        bool is_unsigned = instruction.kind == EMU_INST_UMADDL || instruction.kind == EMU_INST_UMSUBL;
        bool is_sub = instruction.kind == EMU_INST_SMSUBL || instruction.kind == EMU_INST_UMSUBL;
        uint64_t lhs = cpu_read_register(cpu, instruction.rn) & 0xffffffffull;
        uint64_t rhs = cpu_read_register(cpu, instruction.rm) & 0xffffffffull;
        uint64_t product = 0;
        if (is_unsigned) {
            product = lhs * rhs;
        } else {
            product = (uint64_t)((int64_t)(int32_t)(uint32_t)lhs * (int64_t)(int32_t)(uint32_t)rhs);
        }
        uint64_t acc = cpu_read_register(cpu, instruction.rt2);
        uint64_t value = is_sub ? acc - product : acc + product;
        cpu_write_register(cpu, instruction.rd, true, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_UDIV: {
        uint64_t divisor = read_gp_width(cpu, instruction.rm, instruction.is_64_bit);
        uint64_t value = divisor == 0 ? 0 : read_gp_width(cpu, instruction.rn, instruction.is_64_bit) / divisor;
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_SDIV: {
        uint64_t dividend_bits = read_gp_width(cpu, instruction.rn, instruction.is_64_bit);
        uint64_t divisor_bits = read_gp_width(cpu, instruction.rm, instruction.is_64_bit);
        uint64_t value = 0;
        if (divisor_bits != 0) {
            if (instruction.is_64_bit) {
                int64_t dividend = (int64_t)dividend_bits;
                int64_t divisor = (int64_t)divisor_bits;
                value = (dividend == INT64_MIN && divisor == -1) ? (uint64_t)INT64_MIN : (uint64_t)(dividend / divisor);
            } else {
                int32_t dividend = (int32_t)(uint32_t)dividend_bits;
                int32_t divisor = (int32_t)(uint32_t)divisor_bits;
                value = (dividend == INT32_MIN && divisor == -1) ? (uint32_t)INT32_MIN : (uint32_t)(dividend / divisor);
            }
        }
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_ADR: {
        uint64_t target = 0;
        if (!emu_checked_add_i64(current_pc, instruction.offset, &target)) {
            snprintf(error, error_size, "ADR target overflow: pc=0x%016" PRIx64 " offset=%" PRId64, current_pc,
                     instruction.offset);
            return EMU_ERROR;
        }
        cpu_write_register(cpu, instruction.rd, true, target);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_ADRP: {
        uint64_t page = current_pc & ~0xfffull;
        uint64_t target = 0;
        if (!emu_checked_add_i64(page, instruction.offset, &target)) {
            snprintf(error, error_size, "ADRP target overflow: pc_page=0x%016" PRIx64 " offset=%" PRId64, page,
                     instruction.offset);
            return EMU_ERROR;
        }
        cpu_write_register(cpu, instruction.rd, true, target);
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

    case EMU_INST_BL: {
        uint64_t target = 0;
        if (!cpu_calculate_branch_target(current_pc, instruction.offset, memory, &target, error, error_size)) {
            return EMU_ERROR;
        }
        cpu_write_register(cpu, 30, true, current_pc + 4u);
        cpu->pc = target;
        break;
    }

    case EMU_INST_BR: {
        uint64_t target = cpu_read_register(cpu, instruction.rn);
        if (instruction.rn == 31 || !validate_indirect_branch_target(target, memory, "branch", error, error_size)) {
            return EMU_ERROR;
        }
        cpu->pc = target;
        break;
    }

    case EMU_INST_BLR: {
        uint64_t target = cpu_read_register(cpu, instruction.rn);
        if (instruction.rn == 31 || !validate_indirect_branch_target(target, memory, "branch-with-link", error, error_size)) {
            return EMU_ERROR;
        }
        cpu_write_register(cpu, 30, true, current_pc + 4u);
        cpu->pc = target;
        break;
    }

    case EMU_INST_RET: {
        uint64_t target = cpu_read_register(cpu, instruction.rn);
        if (instruction.rn == 31 || target < EMU_LOAD_ADDRESS) {
            snprintf(error, error_size, "invalid return target: x%u=0x%016" PRIx64, (unsigned)instruction.rn,
                     target);
            return EMU_ERROR;
        }
        if ((target & 0x3ull) != 0) {
            snprintf(error, error_size, "misaligned return target: x%u=0x%016" PRIx64, (unsigned)instruction.rn,
                     target);
            return EMU_ERROR;
        }
        if (target > (uint64_t)memory->size || memory->size - (size_t)target < sizeof(uint32_t)) {
            snprintf(error, error_size, "return target outside memory: x%u=0x%016" PRIx64, (unsigned)instruction.rn,
                     target);
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

    case EMU_INST_TBZ:
    case EMU_INST_TBNZ: {
        uint64_t value = cpu_read_register(cpu, instruction.rn);
        bool bit_set = ((value >> instruction.shift_amount) & 0x1u) != 0;
        bool should_branch = instruction.kind == EMU_INST_TBZ ? !bit_set : bit_set;
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

    case EMU_INST_CSEL: {
        uint64_t value = 0;
        if (cpu_condition_passed(cpu->flags, instruction.condition)) {
            value = read_gp_width(cpu, instruction.rn, instruction.is_64_bit);
        } else {
            value = read_gp_width(cpu, instruction.rm, instruction.is_64_bit);
            switch (instruction.shift_type) {
            case 0: /* CSEL */
                break;
            case 1: /* CSINC */
                value++;
                break;
            case 2: /* CSINV */
                value = ~value;
                break;
            case 3: /* CSNEG */
                value = 0 - value;
                break;
            }
        }
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
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

    case EMU_INST_LDR_LITERAL: {
        uint64_t address = 0;
        uint64_t value = 0;
        if (!emu_checked_add_i64(current_pc, instruction.offset, &address)) {
            snprintf(error, error_size, "literal load address overflow: pc=0x%016" PRIx64 " offset=%" PRId64,
                     current_pc, instruction.offset);
            return EMU_ERROR;
        }
        if (!memory_read_width(memory, address, instruction.access_size, &value, error, error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
            return EMU_ERROR;
        }
        if (instruction.sign_extend) {
            value = (uint64_t)sign_extend_width(value, instruction.access_size * 8u);
        }
        cpu_write_register(cpu, instruction.rd, instruction.is_64_bit, value);
        cpu->pc += 4;
        break;
    }

    case EMU_INST_LDR:
    case EMU_INST_LDUR: {
        uint64_t address = 0;
        uint64_t writeback_value = 0;
        bool has_writeback = false;
        uint64_t value = 0;

        if (!cpu_calculate_memory_access(cpu, &instruction, memory, &address, &writeback_value, &has_writeback, error,
                                         error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
            return EMU_ERROR;
        }

        if (!memory_read_width(memory, address, instruction.access_size, &value, error, error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
            return EMU_ERROR;
        }
        if (instruction.sign_extend) {
            value = (uint64_t)sign_extend_width(value, instruction.access_size * 8u);
        }
        cpu_write_register(cpu, instruction.rd, instruction.sign_extend ? instruction.is_64_bit : instruction.access_size == 8,
                           value);

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

        if (!cpu_calculate_memory_access(cpu, &instruction, memory, &address, &writeback_value, &has_writeback, error,
                                         error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
            return EMU_ERROR;
        }

        if (!memory_write_width(memory, address, instruction.access_size, cpu_read_register(cpu, instruction.rd), error,
                                error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
            return EMU_ERROR;
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

        if (!cpu_calculate_memory_access(cpu, &instruction, memory, &address, &writeback_value, &has_writeback, error,
                                         error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
            return EMU_ERROR;
        }
        if (!memory_check_read(memory, address, 16, error, error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
            return EMU_ERROR;
        }
        if (!memory_read64(memory, address, &first, error, error_size) ||
            !memory_read64(memory, address + 8u, &second, error, error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
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

        if (!cpu_calculate_memory_access(cpu, &instruction, memory, &address, &writeback_value, &has_writeback, error,
                                         error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
            return EMU_ERROR;
        }
        if (!memory_check_write(memory, address, 16, error, error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
            return EMU_ERROR;
        }

        if (!memory_write64(memory, address, cpu_read_register(cpu, instruction.rd), error, error_size) ||
            !memory_write64(memory, address + 8u, cpu_read_register(cpu, instruction.rt2), error, error_size)) {
            add_instruction_context(error, error_size, current_pc, opcode);
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

EmuStatus cpu_step(Cpu *cpu, Memory *memory, char *error, size_t error_size) {
    uint32_t opcode = 0;
    EmuDecodedInstruction instruction;

    if (!cpu_fetch_decode(cpu, memory, &opcode, &instruction, error, error_size)) {
        return EMU_ERROR;
    }

    return cpu_execute_decoded(cpu, memory, opcode, &instruction, error, error_size);
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
