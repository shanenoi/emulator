#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define TMP_DIR "tests/phase0/tmp/"

#define OP_NOP 0xd503201fu
#define OP_HLT 0xd4400000u
#define OP_SVC_0 0xd4000001u
#define OP_SVC_1 0xd4000021u
#define OP_ERET 0xd69f03e0u
#define OP_UNSUPPORTED 0xffffffffu

#define ELF64_EHDR_SIZE 64u
#define ELF64_PHDR_SIZE 56u
#define ELF_FIXTURE_CAPACITY 8192u

#define ELF_EI_CLASS 4u
#define ELF_EI_DATA 5u
#define ELF_EI_VERSION 6u
#define ELF_E_TYPE 16u
#define ELF_E_MACHINE 18u
#define ELF_E_VERSION 20u
#define ELF_E_ENTRY 24u
#define ELF_E_PHOFF 32u
#define ELF_E_EHSIZE 52u
#define ELF_E_PHENTSIZE 54u
#define ELF_E_PHNUM 56u

#define ELF_P_TYPE 0u
#define ELF_P_FLAGS 4u
#define ELF_P_OFFSET 8u
#define ELF_P_VADDR 16u
#define ELF_P_FILESZ 32u
#define ELF_P_MEMSZ 40u
#define ELF_P_ALIGN 48u

#define MACHO_HEADER_SIZE 32u
#define MACHO_SEGMENT_COMMAND_SIZE 72u
#define MACHO_MAIN_COMMAND_SIZE 24u
#define MACHO_LC_SEGMENT_64 0x19u
#define MACHO_LC_MAIN 0x80000028u
#define MACHO_VM_PROT_READ 0x1u
#define MACHO_VM_PROT_WRITE 0x2u
#define MACHO_VM_PROT_EXECUTE 0x4u
#define MACHO_FIXTURE_CAPACITY 8192u

static int tests_run = 0;
static int tests_failed = 0;

static void fail_at(const char *file, int line, const char *expr) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    tests_failed++;
}

#define EXPECT_TRUE(expr) do { tests_run++; if (!(expr)) fail_at(__FILE__, __LINE__, #expr); } while (0)
#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))
#define EXPECT_U64_EQ(actual, expected) do { \
    tests_run++; \
    uint64_t actual_value__ = (uint64_t)(actual); \
    uint64_t expected_value__ = (uint64_t)(expected); \
    if (actual_value__ != expected_value__) { \
        fprintf(stderr, "FAIL %s:%d: %s == 0x%016" PRIx64 ", expected 0x%016" PRIx64 "\n", \
                __FILE__, __LINE__, #actual, actual_value__, expected_value__); \
        tests_failed++; \
    } \
} while (0)
#define EXPECT_SIZE_EQ(actual, expected) EXPECT_U64_EQ((actual), (expected))
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

typedef struct {
    uint8_t bytes[ELF_FIXTURE_CAPACITY];
    size_t size;
} ElfFixture;

typedef struct {
    uint8_t bytes[MACHO_FIXTURE_CAPACITY];
    size_t size;
} MachOFixture;

static void write_le16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)(value & 0xffu);
    bytes[offset + 1u] = (uint8_t)((value >> 8u) & 0xffu);
}

static void write_le32(uint8_t *bytes, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4u; i++) {
        bytes[offset + i] = (uint8_t)((value >> (8u * i)) & 0xffu);
    }
}

static void write_le64(uint8_t *bytes, size_t offset, uint64_t value) {
    for (size_t i = 0; i < 8u; i++) {
        bytes[offset + i] = (uint8_t)((value >> (8u * i)) & 0xffu);
    }
}

static uint32_t encode_movz(uint8_t rd, uint16_t imm16, unsigned shift, bool is_64_bit) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x52800000u | (((uint32_t)(shift / 16u) & 0x3u) << 21u) |
           ((uint32_t)imm16 << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_add_imm(uint8_t rd, uint8_t rn, uint16_t imm12, bool is_64_bit, bool sets_flags) {
    return (is_64_bit ? 0x80000000u : 0u) | (sets_flags ? 0x31000000u : 0x11000000u) |
           ((uint32_t)(imm12 & 0xfffu) << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_sub_imm(uint8_t rd, uint8_t rn, uint16_t imm12, bool is_64_bit, bool sets_flags) {
    return (is_64_bit ? 0x80000000u : 0u) | (sets_flags ? 0x71000000u : 0x51000000u) |
           ((uint32_t)(imm12 & 0xfffu) << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_cmp_reg(uint8_t rn, uint8_t rm, bool is_64_bit) {
    return (is_64_bit ? 0x80000000u : 0u) | 0x6b000000u | ((uint32_t)(rm & 0x1fu) << 16u) |
           ((uint32_t)(rn & 0x1fu) << 5u) | 0x1fu;
}

static uint32_t encode_bcond(int64_t offset, EmuCondition condition) {
    return 0x54000000u | ((uint32_t)((offset / 4) & 0x7ffffu) << 5u) | (uint32_t)condition;
}

static uint32_t encode_ldr_unsigned(uint8_t rt, uint8_t rn, uint16_t scaled_offset, bool is_64_bit) {
    return (is_64_bit ? 0xf9400000u : 0xb9400000u) | ((uint32_t)(scaled_offset & 0xfffu) << 10u) |
           ((uint32_t)(rn & 0x1fu) << 5u) | (uint32_t)(rt & 0x1fu);
}

static uint32_t encode_str_unsigned(uint8_t rt, uint8_t rn, uint16_t scaled_offset, bool is_64_bit) {
    return (is_64_bit ? 0xf9000000u : 0xb9000000u) | ((uint32_t)(scaled_offset & 0xfffu) << 10u) |
           ((uint32_t)(rn & 0x1fu) << 5u) | (uint32_t)(rt & 0x1fu);
}

static uint32_t encode_brk(uint16_t imm16) {
    return 0xd4200000u | ((uint32_t)imm16 << 5u);
}

static void put_op_raw(Memory *memory, uint64_t address, uint32_t opcode) {
    size_t offset = (size_t)address;
    memory->bytes[offset] = (uint8_t)(opcode & 0xffu);
    memory->bytes[offset + 1u] = (uint8_t)((opcode >> 8u) & 0xffu);
    memory->bytes[offset + 2u] = (uint8_t)((opcode >> 16u) & 0xffu);
    memory->bytes[offset + 3u] = (uint8_t)((opcode >> 24u) & 0xffu);
}

static void put_op_checked(Memory *memory, uint64_t address, uint32_t opcode) {
    char error[256];
    if (!memory_write32(memory, address, opcode, error, sizeof(error))) {
        fprintf(stderr, "memory_write32 failed: %s\n", error);
        exit(1);
    }
}

static void init_memory_or_die(Memory *memory) {
    char error[256];
    if (!memory_init(memory, EMU_MEMORY_SIZE, error, sizeof(error))) {
        fprintf(stderr, "memory_init failed: %s\n", error);
        exit(1);
    }
}

static void init_cpu_memory_or_die(Cpu *cpu, Memory *memory) {
    init_memory_or_die(memory);
    cpu_init(cpu, EMU_LOAD_ADDRESS, EMU_MEMORY_SIZE);
}

static void init_emulator_or_die(Emulator *emu) {
    char error[512];
    if (!emulator_init(emu, error, sizeof(error))) {
        fprintf(stderr, "emulator_init failed: %s\n", error);
        exit(1);
    }
}

static void map_or_die(Memory *memory, uint64_t address, uint64_t length, uint8_t permissions, const char *name) {
    char error[512];
    if (!memory_map_range(memory, address, length, permissions, name, error, sizeof(error))) {
        fprintf(stderr, "memory_map_range failed: %s\n", error);
        exit(1);
    }
}

static void init_mapped_exception_emulator_or_die(Emulator *emu, uint64_t vector) {
    char error[512];
    init_emulator_or_die(emu);
    map_or_die(&emu->memory, EMU_LOAD_ADDRESS, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_EXEC, "text");
    map_or_die(&emu->memory, vector, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_EXEC, "vector");
    put_op_raw(&emu->memory, vector, OP_HLT);
    if (!emulator_configure_exception_vector(emu, vector, error, sizeof(error))) {
        fprintf(stderr, "emulator_configure_exception_vector failed: %s\n", error);
        exit(1);
    }
}

static FILE *tmpfile_or_die(void) {
    FILE *file = tmpfile();
    if (file == NULL) {
        perror("tmpfile");
        exit(1);
    }
    return file;
}

static void ensure_tmp_dir(void) {
    (void)mkdir("tests/phase0", 0777);
    (void)mkdir("tests/phase0/tmp", 0777);
}

static size_t read_stream(FILE *file, uint8_t *out, size_t capacity) {
    fflush(file);
    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("fseek");
        exit(1);
    }
    return fread(out, 1, capacity, file);
}

static void write_file_or_die(const char *path, const uint8_t *bytes, size_t size) {
    FILE *file = fopen(path, "wb");
    if (file == NULL) {
        perror(path);
        exit(1);
    }
    if (fwrite(bytes, 1, size, file) != size) {
        perror("fwrite");
        fclose(file);
        exit(1);
    }
    fclose(file);
}

static void elf_fixture_init(ElfFixture *fixture, uint64_t entry, uint16_t phnum) {
    memset(fixture, 0, sizeof(*fixture));
    fixture->bytes[0] = EMU_ELF_MAGIC0;
    fixture->bytes[1] = EMU_ELF_MAGIC1;
    fixture->bytes[2] = EMU_ELF_MAGIC2;
    fixture->bytes[3] = EMU_ELF_MAGIC3;
    fixture->bytes[ELF_EI_CLASS] = EMU_ELF_CLASS_64;
    fixture->bytes[ELF_EI_DATA] = EMU_ELF_DATA_LSB;
    fixture->bytes[ELF_EI_VERSION] = EMU_ELF_VERSION_CURRENT;
    write_le16(fixture->bytes, ELF_E_TYPE, EMU_ELF_ET_EXEC);
    write_le16(fixture->bytes, ELF_E_MACHINE, EMU_ELF_EM_AARCH64);
    write_le32(fixture->bytes, ELF_E_VERSION, EMU_ELF_VERSION_CURRENT);
    write_le64(fixture->bytes, ELF_E_ENTRY, entry);
    write_le64(fixture->bytes, ELF_E_PHOFF, ELF64_EHDR_SIZE);
    write_le16(fixture->bytes, ELF_E_EHSIZE, ELF64_EHDR_SIZE);
    write_le16(fixture->bytes, ELF_E_PHENTSIZE, ELF64_PHDR_SIZE);
    write_le16(fixture->bytes, ELF_E_PHNUM, phnum);
    fixture->size = ELF64_EHDR_SIZE + (size_t)phnum * ELF64_PHDR_SIZE;
}

static void elf_fixture_set_ph(ElfFixture *fixture, uint16_t index, uint32_t flags, uint64_t offset, uint64_t vaddr,
                               uint64_t filesz, uint64_t memsz, const uint8_t *data) {
    size_t ph = ELF64_EHDR_SIZE + (size_t)index * ELF64_PHDR_SIZE;
    write_le32(fixture->bytes, ph + ELF_P_TYPE, EMU_ELF_PT_LOAD);
    write_le32(fixture->bytes, ph + ELF_P_FLAGS, flags);
    write_le64(fixture->bytes, ph + ELF_P_OFFSET, offset);
    write_le64(fixture->bytes, ph + ELF_P_VADDR, vaddr);
    write_le64(fixture->bytes, ph + ELF_P_FILESZ, filesz);
    write_le64(fixture->bytes, ph + ELF_P_MEMSZ, memsz);
    write_le64(fixture->bytes, ph + ELF_P_ALIGN, EMU_PAGE_SIZE);
    if (filesz > 0 && data != NULL) {
        memcpy(&fixture->bytes[(size_t)offset], data, (size_t)filesz);
    }
    if (offset + filesz > fixture->size) {
        fixture->size = (size_t)(offset + filesz);
    }
}

static void macho_fixed_name(uint8_t *bytes, size_t offset, const char *name) {
    memset(&bytes[offset], 0, 16u);
    for (size_t i = 0; i < 16u && name[i] != '\0'; i++) {
        bytes[offset + i] = (uint8_t)name[i];
    }
}

static void macho_fixture_init(MachOFixture *fixture, uint32_t ncmds, uint32_t sizeofcmds) {
    memset(fixture, 0, sizeof(*fixture));
    write_le32(fixture->bytes, 0, EMU_MACHO_MAGIC_64);
    write_le32(fixture->bytes, 4, EMU_MACHO_CPU_TYPE_ARM64);
    write_le32(fixture->bytes, 8, 0);
    write_le32(fixture->bytes, 12, EMU_MACHO_FILETYPE_EXECUTE);
    write_le32(fixture->bytes, 16, ncmds);
    write_le32(fixture->bytes, 20, sizeofcmds);
    write_le32(fixture->bytes, 24, 0);
    write_le32(fixture->bytes, 28, 0);
    fixture->size = MACHO_HEADER_SIZE + sizeofcmds;
}

static void macho_fixture_set_segment(MachOFixture *fixture, size_t command_offset, const char *name,
                                      uint64_t vmaddr, uint64_t vmsize, uint64_t fileoff, uint64_t filesize,
                                      uint32_t initprot, const uint8_t *data) {
    write_le32(fixture->bytes, command_offset, MACHO_LC_SEGMENT_64);
    write_le32(fixture->bytes, command_offset + 4u, MACHO_SEGMENT_COMMAND_SIZE);
    macho_fixed_name(fixture->bytes, command_offset + 8u, name);
    write_le64(fixture->bytes, command_offset + 24u, vmaddr);
    write_le64(fixture->bytes, command_offset + 32u, vmsize);
    write_le64(fixture->bytes, command_offset + 40u, fileoff);
    write_le64(fixture->bytes, command_offset + 48u, filesize);
    write_le32(fixture->bytes, command_offset + 56u, MACHO_VM_PROT_READ | MACHO_VM_PROT_WRITE | MACHO_VM_PROT_EXECUTE);
    write_le32(fixture->bytes, command_offset + 60u, initprot);
    write_le32(fixture->bytes, command_offset + 64u, 0);
    write_le32(fixture->bytes, command_offset + 68u, 0);
    if (filesize > 0 && data != NULL) {
        memcpy(&fixture->bytes[(size_t)fileoff], data, (size_t)filesize);
    }
    if (fileoff + filesize > fixture->size) {
        fixture->size = (size_t)(fileoff + filesize);
    }
}

static void macho_fixture_set_main(MachOFixture *fixture, size_t command_offset, uint64_t entryoff) {
    write_le32(fixture->bytes, command_offset, MACHO_LC_MAIN);
    write_le32(fixture->bytes, command_offset + 4u, MACHO_MAIN_COMMAND_SIZE);
    write_le64(fixture->bytes, command_offset + 8u, entryoff);
    write_le64(fixture->bytes, command_offset + 16u, 0);
}

static bool load_program_path(Emulator *emu, const char *path, EmuLoadedProgram *program, char *error,
                              size_t error_size) {
    memset(program, 0xa5, sizeof(*program));
    memset(error, 0, error_size);
    return emulator_load_program(emu, path, program, error, error_size);
}

static void test_cpu_decode_execute_characterization(void) {
    Cpu cpu;
    Memory memory;
    EmuDecodedInstruction inst;
    char error[512] = {0};

    EXPECT_TRUE(cpu_decode(OP_NOP, &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_NOP);
    EXPECT_TRUE(cpu_decode(encode_movz(0, 0xffffu, 48u, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_MOVZ);
    EXPECT_U64_EQ(inst.imm, 0xffff000000000000ull);
    EXPECT_TRUE(cpu_decode(encode_add_imm(1, 0, 1, true, false), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_ADD_IMM);
    EXPECT_TRUE(cpu_decode(encode_sub_imm(2, 1, 2, true, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_SUB_IMM);
    EXPECT_TRUE(inst.sets_flags);
    EXPECT_TRUE(cpu_decode(encode_cmp_reg(0, 1, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_CMP_REG);
    EXPECT_TRUE(cpu_decode(encode_bcond(8, EMU_COND_EQ), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_B_COND);
    EXPECT_TRUE(cpu_decode(encode_ldr_unsigned(4, 3, 0, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_LDR);
    EXPECT_TRUE(cpu_decode(encode_str_unsigned(1, 3, 0, true), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_STR);
    EXPECT_TRUE(cpu_decode(OP_SVC_0, &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_SVC);
    EXPECT_TRUE(cpu_decode(encode_brk(0x123u), &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_BRK);
    EXPECT_U64_EQ(inst.imm, 0x123u);
    EXPECT_TRUE(cpu_decode(OP_ERET, &inst, error, sizeof(error)));
    EXPECT_U64_EQ(inst.kind, EMU_INST_ERET);
    EXPECT_FALSE(cpu_decode(OP_UNSUPPORTED, &inst, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported instruction");

    init_cpu_memory_or_die(&cpu, &memory);
    put_op_checked(&memory, cpu.pc, encode_movz(0, 0xffffu, 16u, true));
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 0xffff0000ull);
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS + 4u);
    EXPECT_U64_EQ(cpu.instructions_executed, 1u);

    put_op_checked(&memory, cpu.pc, encode_add_imm(1, 0, 1, true, false));
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 1), 0xffff0001ull);

    cpu_write_register(&cpu, 0, true, 0);
    put_op_checked(&memory, cpu.pc, encode_sub_imm(2, 0, 1, true, true));
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 2), UINT64_MAX);
    EXPECT_TRUE(cpu.flags.n);
    EXPECT_FALSE(cpu.flags.z);
    EXPECT_FALSE(cpu.flags.c);

    cpu_write_register(&cpu, 0, true, 42u);
    cpu_write_register(&cpu, 1, true, 42u);
    put_op_checked(&memory, cpu.pc, encode_cmp_reg(0, 1, true));
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(cpu.flags.z);
    EXPECT_TRUE(cpu.flags.c);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 0), 42u);

    cpu_write_register(&cpu, 3, true, 0x3000u);
    put_op_checked(&memory, cpu.pc, encode_str_unsigned(1, 3, 0, true));
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_OK);
    cpu_write_register(&cpu, 4, true, 0);
    put_op_checked(&memory, cpu.pc, encode_ldr_unsigned(4, 3, 0, true));
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 4), 42u);

    cpu.flags.z = true;
    put_op_checked(&memory, cpu.pc, encode_bcond(8, EMU_COND_EQ));
    uint64_t branch_pc = cpu.pc;
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.pc, branch_pc + 8u);

    put_op_checked(&memory, cpu.pc, OP_SVC_0);
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "svc requires emulator syscall dispatcher");
    EXPECT_U64_EQ(cpu.instructions_executed, 7u);

    put_op_checked(&memory, cpu.pc, OP_HLT);
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(cpu.halted);
    EXPECT_U64_EQ(cpu.instructions_executed, 8u);
    memory_free(&memory);
}

static void test_cpu_fetch_decode_execute_split(void) {
    Cpu cpu;
    Memory memory;
    EmuDecodedInstruction inst;
    uint32_t opcode = 0;
    char error[512] = {0};

    init_cpu_memory_or_die(&cpu, &memory);

    uint32_t movz = encode_movz(5, 0x1234u, 0u, true);
    put_op_checked(&memory, cpu.pc, movz);
    EXPECT_TRUE(cpu_fetch_decode(&cpu, &memory, &opcode, &inst, error, sizeof(error)));
    EXPECT_U64_EQ(opcode, movz);
    EXPECT_U64_EQ(inst.kind, EMU_INST_MOVZ);
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(cpu.instructions_executed, 0u);

    put_op_checked(&memory, cpu.pc, OP_HLT);
    EXPECT_U64_EQ(cpu_execute_decoded(&cpu, &memory, opcode, &inst, error, sizeof(error)), EMU_OK);
    EXPECT_FALSE(cpu.halted);
    EXPECT_U64_EQ(cpu_read_register(&cpu, 5), 0x1234u);
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS + 4u);
    EXPECT_U64_EQ(cpu.instructions_executed, 1u);

    put_op_checked(&memory, cpu.pc, OP_NOP);
    EXPECT_U64_EQ(cpu_step(&cpu, &memory, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu.pc, EMU_LOAD_ADDRESS + 8u);
    EXPECT_U64_EQ(cpu.instructions_executed, 2u);

    put_op_checked(&memory, cpu.pc, OP_SVC_0);
    EXPECT_TRUE(cpu_fetch_decode(&cpu, &memory, &opcode, &inst, error, sizeof(error)));
    EXPECT_U64_EQ(cpu_execute_decoded(&cpu, &memory, opcode, &inst, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "svc requires emulator syscall dispatcher");
    EXPECT_U64_EQ(cpu.instructions_executed, 2u);

    put_op_checked(&memory, cpu.pc, OP_UNSUPPORTED);
    EXPECT_FALSE(cpu_fetch_decode(&cpu, &memory, &opcode, &inst, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "decode error at pc=0x0000000000001008: unsupported instruction");

    memory_free(&memory);
}

static void test_emulator_step_traps_syscalls_and_exceptions(void) {
    const uint64_t vector = 0x2000u;
    Emulator emu;
    char error[512] = {0};

    init_emulator_or_die(&emu);
    put_op_checked(&emu.memory, emu.cpu.pc, OP_NOP);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 4u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1u);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    cpu_write_register(&emu.cpu, 0, true, 7u);
    cpu_write_register(&emu.cpu, 8, true, EMU_SYSCALL_EXIT);
    put_op_checked(&emu.memory, emu.cpu.pc, OP_SVC_0);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.guest_exited);
    EXPECT_U64_EQ(emu.guest_exit_code, 7u);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 4u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1u);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_HALTED);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    put_op_checked(&emu.memory, emu.cpu.pc, encode_brk(0x44u));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "breakpoint-or-trap");
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1u);
    emulator_free(&emu);

    init_mapped_exception_emulator_or_die(&emu, vector);
    put_op_raw(&emu.memory, EMU_LOAD_ADDRESS, encode_brk(0x55u));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, vector);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(emu.exceptions.context.cause, EMU_EXCEPTION_BREAKPOINT_OR_TRAP);
    EXPECT_U64_EQ(emu.exceptions.context.fault_address, 0x55u);
    EXPECT_U64_EQ(emu.exceptions.context.interrupted_pc, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(emu.exceptions.context.resume_pc, EMU_LOAD_ADDRESS + 4u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1u);
    emulator_free(&emu);

    init_mapped_exception_emulator_or_die(&emu, vector);
    put_op_raw(&emu.memory, EMU_LOAD_ADDRESS, OP_SVC_1);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, vector);
    EXPECT_U64_EQ(emu.exceptions.context.cause, EMU_EXCEPTION_SVC_TRAP);
    EXPECT_U64_EQ(emu.exceptions.context.fault_address, 1u);
    EXPECT_U64_EQ(emu.exceptions.context.resume_pc, EMU_LOAD_ADDRESS + 4u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 0u);
    emulator_free(&emu);

    init_mapped_exception_emulator_or_die(&emu, vector);
    put_op_raw(&emu.memory, vector, OP_ERET);
    EXPECT_U64_EQ(emulator_raise_exception(&emu, EMU_EXCEPTION_BREAKPOINT_OR_TRAP, 0x99u, EMU_LOAD_ADDRESS,
                                           EMU_LOAD_ADDRESS + 4u, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, vector);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_FALSE(emu.exceptions.active);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS + 4u);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 1u);
    emulator_free(&emu);

    init_mapped_exception_emulator_or_die(&emu, vector);
    put_op_raw(&emu.memory, EMU_LOAD_ADDRESS, OP_ERET);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(emu.exceptions.active);
    EXPECT_U64_EQ(emu.exceptions.context.cause, EMU_EXCEPTION_INVALID_INSTRUCTION);
    EXPECT_U64_EQ(emu.exceptions.context.interrupted_pc, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(emu.exceptions.context.resume_pc, EMU_LOAD_ADDRESS);
    EXPECT_U64_EQ(emu.cpu.instructions_executed, 0u);
    emulator_free(&emu);
}

static void test_emulator_step_fetch_decode_and_fault_classification(void) {
    const uint64_t vector = 0x2000u;
    Emulator emu;
    char error[512] = {0};

    init_emulator_or_die(&emu);
    map_or_die(&emu.memory, vector, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_EXEC, "vector");
    put_op_raw(&emu.memory, vector, OP_HLT);
    EXPECT_TRUE(emulator_configure_exception_vector(&emu, vector, error, sizeof(error)));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.cpu.pc, vector);
    EXPECT_U64_EQ(emu.exceptions.context.cause, EMU_EXCEPTION_FETCH_FAULT);
    EXPECT_U64_EQ(emu.exceptions.context.fault_address, EMU_LOAD_ADDRESS);
    emulator_free(&emu);

    init_emulator_or_die(&emu);
    map_or_die(&emu.memory, EMU_LOAD_ADDRESS, EMU_PAGE_SIZE, EMU_MAP_READ, "read-only-text");
    map_or_die(&emu.memory, vector, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_EXEC, "vector");
    put_op_raw(&emu.memory, EMU_LOAD_ADDRESS, OP_NOP);
    EXPECT_TRUE(emulator_configure_exception_vector(&emu, vector, error, sizeof(error)));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.exceptions.context.cause, EMU_EXCEPTION_EXEC_PERMISSION_FAULT);
    EXPECT_U64_EQ(emu.exceptions.context.fault_address, EMU_LOAD_ADDRESS);
    emulator_free(&emu);

    init_mapped_exception_emulator_or_die(&emu, vector);
    put_op_raw(&emu.memory, EMU_LOAD_ADDRESS, OP_UNSUPPORTED);
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.exceptions.context.cause, EMU_EXCEPTION_INVALID_INSTRUCTION);
    EXPECT_U64_EQ(emu.exceptions.context.interrupted_pc, EMU_LOAD_ADDRESS);
    emulator_free(&emu);

    init_mapped_exception_emulator_or_die(&emu, vector);
    map_or_die(&emu.memory, 0x4000u, EMU_PAGE_SIZE, EMU_MAP_EXEC, "exec-only-data");
    cpu_write_register(&emu.cpu, 1, true, 0x4000u);
    put_op_raw(&emu.memory, EMU_LOAD_ADDRESS, encode_ldr_unsigned(0, 1, 0, true));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.exceptions.context.cause, EMU_EXCEPTION_READ_PERMISSION_FAULT);
    EXPECT_U64_EQ(emu.exceptions.context.fault_address, 0x4000u);
    emulator_free(&emu);

    init_mapped_exception_emulator_or_die(&emu, vector);
    map_or_die(&emu.memory, 0x5000u, EMU_PAGE_SIZE, EMU_MAP_READ, "read-only-data");
    cpu_write_register(&emu.cpu, 0, true, 0xa5a5u);
    cpu_write_register(&emu.cpu, 1, true, 0x5000u);
    put_op_raw(&emu.memory, EMU_LOAD_ADDRESS, encode_str_unsigned(0, 1, 0, true));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.exceptions.context.cause, EMU_EXCEPTION_WRITE_PERMISSION_FAULT);
    EXPECT_U64_EQ(emu.exceptions.context.fault_address, 0x5000u);
    emulator_free(&emu);

    init_mapped_exception_emulator_or_die(&emu, vector);
    cpu_write_register(&emu.cpu, 1, true, EMU_DEVICE_UART_BASE + EMU_UART_STATUS_OFFSET);
    put_op_raw(&emu.memory, EMU_LOAD_ADDRESS, encode_ldr_unsigned(0, 1, 0, true));
    EXPECT_U64_EQ(emulator_step(&emu, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(emu.exceptions.context.cause, EMU_EXCEPTION_DEVICE_FAULT);
    EXPECT_U64_EQ(emu.exceptions.context.fault_address, EMU_DEVICE_UART_BASE + EMU_UART_STATUS_OFFSET);
    emulator_free(&emu);
}

static void test_mmio_side_effects_and_boundaries(void) {
    Memory memory;
    char error[512] = {0};
    uint8_t out_bytes[16];
    uint32_t word = 0;
    uint64_t wide = 0;
    FILE *uart = tmpfile_or_die();

    init_memory_or_die(&memory);
    memory_set_uart_output(&memory, uart);
    EXPECT_TRUE(memory_write8(&memory, EMU_DEVICE_UART_BASE + EMU_UART_DATA_OFFSET, 'U', error, sizeof(error)));
    EXPECT_SIZE_EQ(read_stream(uart, out_bytes, sizeof(out_bytes)), 1u);
    EXPECT_U64_EQ(out_bytes[0], 'U');
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_UART_BASE + EMU_UART_STATUS_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, 1u);
    EXPECT_FALSE(memory_read32(&memory, EMU_DEVICE_UART_BASE + EMU_UART_DATA_OFFSET, &word, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "read from write-only register");
    fclose(uart);

    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_TICKS_LO_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, 0u);
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_TICKS_LO_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, 1u);
    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_RESET_OFFSET, 0, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TIMER_BASE + EMU_TIMER_TICKS_LO_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, 0u);

    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_SEED_OFFSET, 0x12345678u, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_VALUE_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, 0x75432777u);
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_RANDOM_BASE + EMU_RANDOM_VALUE_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, 0xcd305e6au);

    uint8_t input[EMU_KBD_QUEUE_CAPACITY + 2u];
    for (size_t i = 0; i < sizeof(input); i++) {
        input[i] = (uint8_t)('a' + i);
    }
    EXPECT_SIZE_EQ(memory_keyboard_enqueue_bytes(&memory, input, sizeof(input)), EMU_KBD_QUEUE_CAPACITY);
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, EMU_KBD_STATUS_READY | EMU_KBD_STATUS_OVERFLOW);
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_DATA_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, 'a');
    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_CONTROL_OFFSET,
                               EMU_KBD_CONTROL_CLEAR_OVERFLOW, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_KEYBOARD_BASE + EMU_KBD_STATUS_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, EMU_KBD_STATUS_READY);

    EXPECT_TRUE(memory_terminal_configure(&memory, 4, 2, error, sizeof(error)));
    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_TERMINAL_BASE + EMU_TERM_DATA_OFFSET, 'T', error, sizeof(error)));
    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_TERMINAL_BASE + EMU_TERM_DATA_OFFSET, '\n', error, sizeof(error)));
    EXPECT_U64_EQ(memory_terminal_cursor_x(&memory), 0u);
    EXPECT_U64_EQ(memory_terminal_cursor_y(&memory), 1u);
    EXPECT_TRUE(memory_terminal_dirty(&memory));
    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_TERMINAL_BASE + EMU_TERM_INDEX_OFFSET, 0, error, sizeof(error)));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_TERMINAL_BASE + EMU_TERM_CELL_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, 'T');
    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_TERMINAL_BASE + EMU_TERM_CONTROL_OFFSET,
                               EMU_TERM_CONTROL_CLEAR_DIRTY, error, sizeof(error)));
    EXPECT_FALSE(memory_terminal_dirty(&memory));

    memory_advance_frame(&memory);
    EXPECT_U64_EQ(memory_frame_counter(&memory), 1u);
    EXPECT_TRUE(memory_frame_ready(&memory));
    EXPECT_TRUE(memory_read32(&memory, EMU_DEVICE_FRAME_BASE + EMU_FRAME_STATUS_OFFSET, &word, error, sizeof(error)));
    EXPECT_U64_EQ(word, EMU_FRAME_STATUS_READY);
    EXPECT_TRUE(memory_write32(&memory, EMU_DEVICE_FRAME_BASE + EMU_FRAME_CONTROL_OFFSET,
                               EMU_FRAME_CONTROL_CLEAR_READY, error, sizeof(error)));
    EXPECT_FALSE(memory_frame_ready(&memory));

    EXPECT_FALSE(memory_read64(&memory, EMU_DEVICE_TIMER_BASE + EMU_DEVICE_SIZE - 4u, &wide, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "crosses device boundary");
    EXPECT_FALSE(memory_write16(&memory, EMU_DEVICE_FRAME_BASE + EMU_FRAME_STATUS_OFFSET, 1u, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "unsupported write width");

    memory_free(&memory);
}

static void test_structured_memory_fault_metadata(void) {
    Memory memory;
    char error[512] = {0};
    uint64_t wide = 0;

    init_memory_or_die(&memory);
    memory_clear_mappings(&memory);
    map_or_die(&memory, 0x1000u, EMU_PAGE_SIZE, EMU_MAP_READ, "ro-data");

    EXPECT_FALSE(memory_check_execute(&memory, 0x1000u, 4u, error, sizeof(error)));
    const EmuFault *fault = memory_last_fault(&memory);
    EXPECT_TRUE(fault != NULL);
    EXPECT_U64_EQ(fault->kind, EMU_FAULT_PERMISSION);
    EXPECT_U64_EQ(fault->access, EMU_FAULT_ACCESS_EXECUTE);
    EXPECT_U64_EQ(fault->address, 0x1000u);
    EXPECT_U64_EQ(fault->width, 4u);
    EXPECT_STR_CONTAINS(error, "execute permission denied");
    EXPECT_STR_CONTAINS(fault->message, "execute permission denied");

    EXPECT_FALSE(memory_check_read(&memory, 0x3000u, 4u, error, sizeof(error)));
    fault = memory_last_fault(&memory);
    EXPECT_TRUE(fault != NULL);
    EXPECT_U64_EQ(fault->kind, EMU_FAULT_UNMAPPED);
    EXPECT_U64_EQ(fault->access, EMU_FAULT_ACCESS_READ);
    EXPECT_U64_EQ(fault->address, 0x3000u);
    EXPECT_STR_CONTAINS(error, "unmapped access");

    EXPECT_FALSE(memory_read64(&memory, EMU_DEVICE_TIMER_BASE + EMU_DEVICE_SIZE - 4u, &wide, error, sizeof(error)));
    fault = memory_last_fault(&memory);
    EXPECT_TRUE(fault != NULL);
    EXPECT_U64_EQ(fault->kind, EMU_FAULT_DEVICE);
    EXPECT_U64_EQ(fault->access, EMU_FAULT_ACCESS_READ);
    EXPECT_U64_EQ(fault->address, EMU_DEVICE_TIMER_BASE + EMU_DEVICE_SIZE - 4u);
    EXPECT_U64_EQ(fault->width, 8u);
    EXPECT_STR_CONTAINS(error, "crosses device boundary");
    EXPECT_STR_CONTAINS(fault->message, "crosses device boundary");

    EXPECT_FALSE(memory_check_read(&memory, EMU_MEMORY_SIZE, 1u, error, sizeof(error)));
    fault = memory_last_fault(&memory);
    EXPECT_TRUE(fault != NULL);
    EXPECT_U64_EQ(fault->kind, EMU_FAULT_OUT_OF_BOUNDS);
    EXPECT_U64_EQ(fault->access, EMU_FAULT_ACCESS_READ);
    EXPECT_U64_EQ(fault->address, EMU_MEMORY_SIZE);

    EXPECT_TRUE(memory_check_read(&memory, 0x1000u, 1u, error, sizeof(error)));
    fault = memory_last_fault(&memory);
    EXPECT_TRUE(fault != NULL);
    EXPECT_U64_EQ(fault->kind, EMU_FAULT_NONE);

    memory_free(&memory);
}

static void test_loader_mapping_permission_characterization(void) {
    Emulator emu;
    EmuLoadedProgram program;
    ElfFixture elf;
    MachOFixture macho;
    char error[512] = {0};
    const uint8_t hlt_code[4] = {0x00u, 0x00u, 0x40u, 0xd4u};
    const uint8_t data_code[4] = {'D', 'A', 'T', 'A'};

    elf_fixture_init(&elf, 0x1000u, 2u);
    elf_fixture_set_ph(&elf, 0, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x200u, 0x1000u, sizeof(hlt_code), 0x800u, hlt_code);
    elf_fixture_set_ph(&elf, 1, EMU_ELF_PF_W, 0x300u, 0x1800u, sizeof(data_code), 0x100u, data_code);
    write_file_or_die(TMP_DIR "same_page_merge.elf", elf.bytes, elf.size);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, TMP_DIR "same_page_merge.elf", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_ELF64);
    EXPECT_SIZE_EQ(program.segment_count, 2u);
    const EmuMemoryMapping *mapping = memory_find_mapping(&emu.memory, 0x1000u);
    EXPECT_TRUE(mapping != NULL);
    EXPECT_U64_EQ(mapping->start, 0x1000u);
    EXPECT_U64_EQ(mapping->size, EMU_PAGE_SIZE);
    EXPECT_U64_EQ(mapping->permissions, EMU_MAP_READ | EMU_MAP_WRITE | EMU_MAP_EXEC);
    EXPECT_TRUE(memory_check_write(&emu.memory, 0x1000u, 1u, error, sizeof(error)));
    EXPECT_TRUE(memory_check_execute(&emu.memory, 0x1800u, 4u, error, sizeof(error)));
    EXPECT_TRUE(memcmp(&emu.memory.bytes[0x1800u], data_code, sizeof(data_code)) == 0);
    emulator_free(&emu);

    init_memory_or_die(&emu.memory);
    EXPECT_TRUE(memory_map_range(&emu.memory, 0x3000u, EMU_PAGE_SIZE, EMU_MAP_READ | EMU_MAP_EXEC, "text", error,
                                 sizeof(error)));
    EXPECT_FALSE(memory_map_range(&emu.memory, 0x3000u, EMU_PAGE_SIZE, EMU_MAP_WRITE, "data", error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "overlapping mappings are not allowed");
    mapping = memory_find_mapping(&emu.memory, 0x3000u);
    EXPECT_TRUE(mapping != NULL);
    EXPECT_U64_EQ(mapping->permissions, EMU_MAP_READ | EMU_MAP_EXEC);
    memory_free(&emu.memory);

    macho_fixture_init(&macho, 2u, MACHO_SEGMENT_COMMAND_SIZE + MACHO_MAIN_COMMAND_SIZE);
    uint64_t fileoff = MACHO_HEADER_SIZE + MACHO_SEGMENT_COMMAND_SIZE + MACHO_MAIN_COMMAND_SIZE;
    macho_fixture_set_segment(&macho, MACHO_HEADER_SIZE, "__TEXT", 0x1000u, sizeof(hlt_code), fileoff,
                              sizeof(hlt_code), MACHO_VM_PROT_READ | MACHO_VM_PROT_EXECUTE, hlt_code);
    macho_fixture_set_main(&macho, MACHO_HEADER_SIZE + MACHO_SEGMENT_COMMAND_SIZE, fileoff);
    write_file_or_die(TMP_DIR "minimal.macho", macho.bytes, macho.size);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, TMP_DIR "minimal.macho", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_MACHO64);
    EXPECT_U64_EQ(program.entry, 0x1000u);
    EXPECT_SIZE_EQ(program.segment_count, 1u);
    mapping = memory_find_mapping(&emu.memory, 0x1000u);
    EXPECT_TRUE(mapping != NULL);
    EXPECT_U64_EQ(mapping->permissions, EMU_MAP_READ | EMU_MAP_EXEC);
    EXPECT_TRUE(strcmp(mapping->name, "macho:__TEXT") == 0);
    emulator_free(&emu);

    macho_fixture_init(&macho, 3u, (2u * MACHO_SEGMENT_COMMAND_SIZE) + MACHO_MAIN_COMMAND_SIZE);
    fileoff = MACHO_HEADER_SIZE + (2u * MACHO_SEGMENT_COMMAND_SIZE) + MACHO_MAIN_COMMAND_SIZE;
    macho_fixture_set_segment(&macho, MACHO_HEADER_SIZE, "__TEXT", 0x1000u, 0x800u, fileoff, sizeof(hlt_code),
                              MACHO_VM_PROT_READ | MACHO_VM_PROT_EXECUTE, hlt_code);
    macho_fixture_set_segment(&macho, MACHO_HEADER_SIZE + MACHO_SEGMENT_COMMAND_SIZE, "__DATA", 0x1800u, 0x100u,
                              fileoff + 0x100u, sizeof(data_code), MACHO_VM_PROT_READ | MACHO_VM_PROT_WRITE,
                              data_code);
    macho_fixture_set_main(&macho, MACHO_HEADER_SIZE + 2u * MACHO_SEGMENT_COMMAND_SIZE, fileoff);
    write_file_or_die(TMP_DIR "same_page_different_name.macho", macho.bytes, macho.size);

    init_emulator_or_die(&emu);
    EXPECT_FALSE(load_program_path(&emu, TMP_DIR "same_page_different_name.macho", &program, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "overlapping mappings are not allowed");
    emulator_free(&emu);
}

static void test_loader_raw_file_and_stack_characterization(void) {
    Emulator emu;
    EmuLoadedProgram program;
    char error[512] = {0};
    const uint8_t hlt_code[4] = {0x00u, 0x00u, 0x40u, 0xd4u};

    write_file_or_die(TMP_DIR "raw_hlt.bin", hlt_code, sizeof(hlt_code));

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, TMP_DIR "raw_hlt.bin", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_RAW);
    EXPECT_U64_EQ(program.entry, EMU_LOAD_ADDRESS);
    EXPECT_TRUE(memcmp(&emu.memory.bytes[EMU_LOAD_ADDRESS], hlt_code, sizeof(hlt_code)) == 0);

    const EmuMemoryMapping *raw_mapping = memory_find_mapping(&emu.memory, EMU_LOAD_ADDRESS);
    EXPECT_TRUE(raw_mapping != NULL);
    EXPECT_U64_EQ(raw_mapping->permissions, EMU_MAP_READ | EMU_MAP_EXEC);
    EXPECT_TRUE(strcmp(raw_mapping->name, "raw:program") == 0);

    const EmuMemoryMapping *stack_mapping = memory_find_mapping(&emu.memory, program.stack_pointer - 1u);
    EXPECT_TRUE(stack_mapping != NULL);
    EXPECT_U64_EQ(stack_mapping->permissions, EMU_MAP_READ | EMU_MAP_WRITE);
    EXPECT_TRUE(strcmp(stack_mapping->name, "stack") == 0);
    EXPECT_U64_EQ(stack_mapping->start + stack_mapping->size, program.stack_pointer);
    emulator_free(&emu);

    write_file_or_die(TMP_DIR "empty.bin", hlt_code, 0u);
    init_emulator_or_die(&emu);
    EXPECT_FALSE(load_program_path(&emu, TMP_DIR "empty.bin", &program, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "loader error: input file is empty");
    emulator_free(&emu);
}

int main(void) {
    ensure_tmp_dir();

    test_cpu_decode_execute_characterization();
    test_cpu_fetch_decode_execute_split();
    test_emulator_step_traps_syscalls_and_exceptions();
    test_emulator_step_fetch_decode_and_fault_classification();
    test_mmio_side_effects_and_boundaries();
    test_structured_memory_fault_metadata();
    test_loader_mapping_permission_characterization();
    test_loader_raw_file_and_stack_characterization();

    if (tests_failed != 0) {
        fprintf(stderr, "tests/phase0/test_phase0: %d/%d checks failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("tests/phase0/test_phase0: %d checks passed\n", tests_run);
    return 0;
}