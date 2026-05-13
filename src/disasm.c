#include "emulator.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *gp_reg(uint8_t index, bool is_64_bit) {
    static const char *xregs[] = {
        "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",  "x8",  "x9",  "x10",
        "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21",
        "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "xzr"};
    static const char *wregs[] = {
        "w0",  "w1",  "w2",  "w3",  "w4",  "w5",  "w6",  "w7",  "w8",  "w9",  "w10",
        "w11", "w12", "w13", "w14", "w15", "w16", "w17", "w18", "w19", "w20", "w21",
        "w22", "w23", "w24", "w25", "w26", "w27", "w28", "w29", "w30", "wzr"};

    return is_64_bit ? xregs[index & 0x1fu] : wregs[index & 0x1fu];
}

static const char *base_reg(uint8_t index) {
    static const char *xregs[] = {
        "x0",  "x1",  "x2",  "x3",  "x4",  "x5",  "x6",  "x7",  "x8",  "x9",  "x10",
        "x11", "x12", "x13", "x14", "x15", "x16", "x17", "x18", "x19", "x20", "x21",
        "x22", "x23", "x24", "x25", "x26", "x27", "x28", "x29", "x30", "sp"};
    return xregs[index & 0x1fu];
}

static bool addsub_imm_uses_sp(const EmuDecodedInstruction *instruction) {
    if (instruction->sets_flags) {
        return false;
    }
    if (instruction->rd == 31 && instruction->rn == 31) {
        return true;
    }
    return instruction->rd == 29 && instruction->rn == 31 && instruction->imm == 0;
}

static const char *addsub_imm_reg(const EmuDecodedInstruction *instruction, uint8_t index) {
    if (addsub_imm_uses_sp(instruction) && index == 31) {
        return instruction->is_64_bit ? "sp" : "wsp";
    }
    return gp_reg(index, instruction->is_64_bit);
}

static const char *addsub_imm_dest_reg(const EmuDecodedInstruction *instruction) {
    if (addsub_imm_uses_sp(instruction) && instruction->rd == 31) {
        return instruction->is_64_bit ? "sp" : "wsp";
    }
    return gp_reg(instruction->rd, instruction->is_64_bit);
}

static const char *condition_name(EmuCondition condition) {
    switch (condition) {
    case EMU_COND_EQ:
        return "eq";
    case EMU_COND_NE:
        return "ne";
    case EMU_COND_CS:
        return "cs";
    case EMU_COND_CC:
        return "cc";
    case EMU_COND_MI:
        return "mi";
    case EMU_COND_PL:
        return "pl";
    case EMU_COND_VS:
        return "vs";
    case EMU_COND_VC:
        return "vc";
    case EMU_COND_HI:
        return "hi";
    case EMU_COND_LS:
        return "ls";
    case EMU_COND_GE:
        return "ge";
    case EMU_COND_LT:
        return "lt";
    case EMU_COND_GT:
        return "gt";
    case EMU_COND_LE:
        return "le";
    case EMU_COND_AL:
        return "al";
    }
    return "??";
}

static const char *shift_name(uint8_t shift_type) {
    switch (shift_type) {
    case 0:
        return "lsl";
    case 1:
        return "lsr";
    case 2:
        return "asr";
    default:
        return "shift";
    }
}

static const char *load_store_mnemonic(EmuInstructionKind kind, uint8_t access_size) {
    bool is_load = kind == EMU_INST_LDR || kind == EMU_INST_LDUR;
    if (access_size == 1) {
        return is_load ? "ldrb" : "strb";
    }
    if (access_size == 2) {
        return is_load ? "ldrh" : "strh";
    }
    if (kind == EMU_INST_LDUR || kind == EMU_INST_STUR) {
        return is_load ? "ldur" : "stur";
    }
    return is_load ? "ldr" : "str";
}

static bool add_signed_address(uint64_t address, int64_t offset, uint64_t *target) {
    if (offset < 0) {
        uint64_t magnitude = (uint64_t)(-(offset + 1)) + 1ull;
        if (magnitude > address) {
            *target = 0;
            return false;
        }
        *target = address - magnitude;
        return true;
    }

    uint64_t magnitude = (uint64_t)offset;
    if (magnitude > UINT64_MAX - address) {
        *target = UINT64_MAX;
        return false;
    }
    *target = address + magnitude;
    return true;
}

static void format_target(char *buffer, size_t buffer_size, uint64_t address, int64_t offset) {
    uint64_t target = 0;
    if (add_signed_address(address, offset, &target)) {
        snprintf(buffer, buffer_size, "0x%016" PRIx64, target);
    } else {
        snprintf(buffer, buffer_size, "pc%+" PRId64, offset);
    }
}

static void format_memory_operand(const EmuDecodedInstruction *instruction, char *out, size_t out_size) {
    const char *base = base_reg(instruction->rn);
    const char *rt = gp_reg(instruction->rd, instruction->is_64_bit);

    switch (instruction->address_mode) {
    case EMU_ADDR_UNSIGNED_OFFSET:
    case EMU_ADDR_UNSCALED:
        if (instruction->offset == 0) {
            snprintf(out, out_size, "%s, [%s]", rt, base);
        } else {
            snprintf(out, out_size, "%s, [%s, #%" PRId64 "]", rt, base, instruction->offset);
        }
        break;
    case EMU_ADDR_PRE_INDEX:
        snprintf(out, out_size, "%s, [%s, #%" PRId64 "]!", rt, base, instruction->offset);
        break;
    case EMU_ADDR_POST_INDEX:
        snprintf(out, out_size, "%s, [%s], #%" PRId64, rt, base, instruction->offset);
        break;
    case EMU_ADDR_PAIR_OFFSET:
        if (instruction->offset == 0) {
            snprintf(out, out_size, "%s, %s, [%s]", gp_reg(instruction->rd, true), gp_reg(instruction->rt2, true),
                     base);
        } else {
            snprintf(out, out_size, "%s, %s, [%s, #%" PRId64 "]", gp_reg(instruction->rd, true),
                     gp_reg(instruction->rt2, true), base, instruction->offset);
        }
        break;
    }
}

static void format_pair_memory_operand(const EmuDecodedInstruction *instruction, char *out, size_t out_size) {
    const char *base = base_reg(instruction->rn);
    const char *rt = gp_reg(instruction->rd, true);
    const char *rt2 = gp_reg(instruction->rt2, true);

    switch (instruction->address_mode) {
    case EMU_ADDR_PRE_INDEX:
        snprintf(out, out_size, "%s, %s, [%s, #%" PRId64 "]!", rt, rt2, base, instruction->offset);
        break;
    case EMU_ADDR_POST_INDEX:
        snprintf(out, out_size, "%s, %s, [%s], #%" PRId64, rt, rt2, base, instruction->offset);
        break;
    case EMU_ADDR_PAIR_OFFSET:
        if (instruction->offset == 0) {
            snprintf(out, out_size, "%s, %s, [%s]", rt, rt2, base);
        } else {
            snprintf(out, out_size, "%s, %s, [%s, #%" PRId64 "]", rt, rt2, base, instruction->offset);
        }
        break;
    case EMU_ADDR_UNSIGNED_OFFSET:
    case EMU_ADDR_UNSCALED:
        snprintf(out, out_size, "%s, %s, [%s, #%" PRId64 "]", rt, rt2, base, instruction->offset);
        break;
    }
}

static bool format_decoded_text(const EmuDecodedInstruction *instruction, uint64_t address, char *out, size_t out_size) {
    char target[64];
    char operand[128];
    int written = 0;

    switch (instruction->kind) {
    case EMU_INST_NOP:
        written = snprintf(out, out_size, "nop");
        break;
    case EMU_INST_HLT:
        written = snprintf(out, out_size, "hlt #0x%llx", (unsigned long long)instruction->imm);
        break;
    case EMU_INST_SVC:
        written = snprintf(out, out_size, "svc #0x%llx", (unsigned long long)instruction->imm);
        break;
    case EMU_INST_BRK:
        written = snprintf(out, out_size, "brk #0x%llx", (unsigned long long)instruction->imm);
        break;
    case EMU_INST_ERET:
        written = snprintf(out, out_size, "eret");
        break;
    case EMU_INST_MOVN:
        if (instruction->shift_amount == 0) {
            written = snprintf(out, out_size, "movn %s, #0x%llx", gp_reg(instruction->rd, instruction->is_64_bit),
                               (unsigned long long)(instruction->imm & 0xffffull));
        } else {
            written = snprintf(out, out_size, "movn %s, #0x%llx, lsl #%u",
                               gp_reg(instruction->rd, instruction->is_64_bit),
                               (unsigned long long)((instruction->imm >> instruction->shift_amount) & 0xffffull),
                               (unsigned)instruction->shift_amount);
        }
        break;
    case EMU_INST_MOVZ:
        written = snprintf(out, out_size, "movz %s, #0x%llx", gp_reg(instruction->rd, instruction->is_64_bit),
                           (unsigned long long)instruction->imm);
        break;
    case EMU_INST_MOVK:
        if (instruction->shift_amount == 0) {
            written = snprintf(out, out_size, "movk %s, #0x%llx", gp_reg(instruction->rd, instruction->is_64_bit),
                               (unsigned long long)(instruction->imm & 0xffffull));
        } else {
            written = snprintf(out, out_size, "movk %s, #0x%llx, lsl #%u",
                               gp_reg(instruction->rd, instruction->is_64_bit),
                               (unsigned long long)((instruction->imm >> instruction->shift_amount) & 0xffffull),
                               (unsigned)instruction->shift_amount);
        }
        break;
    case EMU_INST_ADD_IMM:
        written = snprintf(out, out_size, "%s %s, %s, #0x%llx", instruction->sets_flags ? "adds" : "add",
                           addsub_imm_dest_reg(instruction), addsub_imm_reg(instruction, instruction->rn),
                           (unsigned long long)instruction->imm);
        break;
    case EMU_INST_SUB_IMM:
        written = snprintf(out, out_size, "%s %s, %s, #0x%llx", instruction->sets_flags ? "subs" : "sub",
                           addsub_imm_dest_reg(instruction), addsub_imm_reg(instruction, instruction->rn),
                           (unsigned long long)instruction->imm);
        break;
    case EMU_INST_ADD_REG:
    case EMU_INST_SUB_REG:
    case EMU_INST_AND_REG:
    case EMU_INST_ORR_REG:
    case EMU_INST_EOR_REG: {
        const char *mnemonic = instruction->kind == EMU_INST_ADD_REG ? (instruction->sets_flags ? "adds" : "add") :
                               instruction->kind == EMU_INST_SUB_REG ? (instruction->sets_flags ? "subs" : "sub") :
                               instruction->kind == EMU_INST_AND_REG ? "and" :
                               instruction->kind == EMU_INST_ORR_REG ? "orr" : "eor";
        if (instruction->shift_amount == 0 && instruction->shift_type == 0) {
            written = snprintf(out, out_size, "%s %s, %s, %s", mnemonic,
                               gp_reg(instruction->rd, instruction->is_64_bit),
                               gp_reg(instruction->rn, instruction->is_64_bit),
                               gp_reg(instruction->rm, instruction->is_64_bit));
        } else {
            written = snprintf(out, out_size, "%s %s, %s, %s, %s #%u", mnemonic,
                               gp_reg(instruction->rd, instruction->is_64_bit),
                               gp_reg(instruction->rn, instruction->is_64_bit),
                               gp_reg(instruction->rm, instruction->is_64_bit), shift_name(instruction->shift_type),
                               (unsigned)instruction->shift_amount);
        }
        break;
    }
    case EMU_INST_LSL_IMM:
    case EMU_INST_LSR_IMM:
    case EMU_INST_ASR_IMM: {
        const char *mnemonic = instruction->kind == EMU_INST_LSL_IMM ? "lsl" :
                               instruction->kind == EMU_INST_LSR_IMM ? "lsr" : "asr";
        written = snprintf(out, out_size, "%s %s, %s, #%u", mnemonic,
                           gp_reg(instruction->rd, instruction->is_64_bit),
                           gp_reg(instruction->rn, instruction->is_64_bit), (unsigned)instruction->shift_amount);
        break;
    }
    case EMU_INST_MUL:
        written = snprintf(out, out_size, "mul %s, %s, %s", gp_reg(instruction->rd, instruction->is_64_bit),
                           gp_reg(instruction->rn, instruction->is_64_bit), gp_reg(instruction->rm, instruction->is_64_bit));
        break;
    case EMU_INST_UDIV:
        written = snprintf(out, out_size, "udiv %s, %s, %s", gp_reg(instruction->rd, instruction->is_64_bit),
                           gp_reg(instruction->rn, instruction->is_64_bit), gp_reg(instruction->rm, instruction->is_64_bit));
        break;
    case EMU_INST_SDIV:
        written = snprintf(out, out_size, "sdiv %s, %s, %s", gp_reg(instruction->rd, instruction->is_64_bit),
                           gp_reg(instruction->rn, instruction->is_64_bit), gp_reg(instruction->rm, instruction->is_64_bit));
        break;
    case EMU_INST_ADR:
        format_target(target, sizeof(target), address, instruction->offset);
        written = snprintf(out, out_size, "adr %s, %s", gp_reg(instruction->rd, true), target);
        break;
    case EMU_INST_ADRP: {
        uint64_t page = address & ~0xfffull;
        format_target(target, sizeof(target), page, instruction->offset);
        written = snprintf(out, out_size, "adrp %s, %s", gp_reg(instruction->rd, true), target);
        break;
    }
    case EMU_INST_B:
        format_target(target, sizeof(target), address, instruction->offset);
        written = snprintf(out, out_size, "b %s", target);
        break;
    case EMU_INST_BL:
        format_target(target, sizeof(target), address, instruction->offset);
        written = snprintf(out, out_size, "bl %s", target);
        break;
    case EMU_INST_B_COND:
        format_target(target, sizeof(target), address, instruction->offset);
        written = snprintf(out, out_size, "b.%s %s", condition_name(instruction->condition), target);
        break;
    case EMU_INST_CBZ:
        format_target(target, sizeof(target), address, instruction->offset);
        written = snprintf(out, out_size, "cbz %s, %s", gp_reg(instruction->rn, instruction->is_64_bit), target);
        break;
    case EMU_INST_CBNZ:
        format_target(target, sizeof(target), address, instruction->offset);
        written = snprintf(out, out_size, "cbnz %s, %s", gp_reg(instruction->rn, instruction->is_64_bit), target);
        break;
    case EMU_INST_CMP_IMM:
        written = snprintf(out, out_size, "cmp %s, #0x%llx", gp_reg(instruction->rn, instruction->is_64_bit),
                           (unsigned long long)instruction->imm);
        break;
    case EMU_INST_CMP_REG:
        written = snprintf(out, out_size, "cmp %s, %s", gp_reg(instruction->rn, instruction->is_64_bit),
                           gp_reg(instruction->rm, instruction->is_64_bit));
        break;
    case EMU_INST_LDR:
        format_memory_operand(instruction, operand, sizeof(operand));
        written = snprintf(out, out_size, "%s %s", load_store_mnemonic(instruction->kind, instruction->access_size), operand);
        break;
    case EMU_INST_STR:
        format_memory_operand(instruction, operand, sizeof(operand));
        written = snprintf(out, out_size, "%s %s", load_store_mnemonic(instruction->kind, instruction->access_size), operand);
        break;
    case EMU_INST_LDUR:
        format_memory_operand(instruction, operand, sizeof(operand));
        written = snprintf(out, out_size, "%s %s", load_store_mnemonic(instruction->kind, instruction->access_size), operand);
        break;
    case EMU_INST_STUR:
        format_memory_operand(instruction, operand, sizeof(operand));
        written = snprintf(out, out_size, "%s %s", load_store_mnemonic(instruction->kind, instruction->access_size), operand);
        break;
    case EMU_INST_LDP:
        format_pair_memory_operand(instruction, operand, sizeof(operand));
        written = snprintf(out, out_size, "ldp %s", operand);
        break;
    case EMU_INST_STP:
        format_pair_memory_operand(instruction, operand, sizeof(operand));
        written = snprintf(out, out_size, "stp %s", operand);
        break;
    case EMU_INST_RET:
        if (instruction->rn == 30) {
            written = snprintf(out, out_size, "ret");
        } else {
            written = snprintf(out, out_size, "ret %s", gp_reg(instruction->rn, true));
        }
        break;
    }

    return written >= 0 && (size_t)written < out_size;
}

bool cpu_format_instruction(uint32_t opcode, uint64_t address, char *out, size_t out_size) {
    if (out == NULL || out_size == 0) {
        return false;
    }

    EmuDecodedInstruction instruction;
    char error[128];
    char text[160];
    bool decoded = cpu_decode(opcode, &instruction, error, sizeof(error));

    if (decoded) {
        if (!format_decoded_text(&instruction, address, text, sizeof(text))) {
            return false;
        }
    } else {
        snprintf(text, sizeof(text), "<unsupported>");
    }

    int written = snprintf(out, out_size, "0x%016" PRIx64 ": 0x%08x  %s", address, opcode, text);
    return decoded && written >= 0 && (size_t)written < out_size;
}
