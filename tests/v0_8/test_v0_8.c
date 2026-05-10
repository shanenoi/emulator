#include "emulator.h"

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define OP_HLT_0 0xd4400000u
#define OP_NOP 0xd503201fu
#define OP_SVC_0 0xd4000001u
#define OP_SVC_1 0xd4000021u
#define OP_UNSUPPORTED 0xffffffffu

#define ELF64_EHDR_SIZE 64u
#define ELF64_PHDR_SIZE 56u
#define FIXTURE_CAPACITY 8192u

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

#define EMU_ELF_PT_NOTE 4u

static int tests_run = 0;
static int tests_failed = 0;

static void fail_at(const char *file, int line, const char *expr) {
    fprintf(stderr, "FAIL %s:%d: %s\n", file, line, expr);
    tests_failed++;
}

#define EXPECT_TRUE(expr) do { tests_run++; if (!(expr)) fail_at(__FILE__, __LINE__, #expr); } while (0)
#define EXPECT_FALSE(expr) EXPECT_TRUE(!(expr))
#define EXPECT_STATUS(actual, expected) EXPECT_U64_EQ((actual), (expected))
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
#define EXPECT_MEM_EQ(actual, expected, length) do { \
    tests_run++; \
    if (memcmp((actual), (expected), (length)) != 0) { \
        fprintf(stderr, "FAIL %s:%d: memory differs for %s\n", __FILE__, __LINE__, #actual); \
        tests_failed++; \
    } \
} while (0)
#define EXPECT_STR_CONTAINS(haystack, needle) do { \
    tests_run++; \
    if (strstr((haystack), (needle)) == NULL) { \
        fprintf(stderr, "FAIL %s:%d: expected string to contain '%s', got '%s'\n", \
                __FILE__, __LINE__, (needle), (haystack)); \
        tests_failed++; \
    } \
} while (0)

typedef struct {
    uint8_t bytes[FIXTURE_CAPACITY];
    size_t size;
} ElfFixture;

static void write_le16(uint8_t *bytes, size_t offset, uint16_t value) {
    bytes[offset] = (uint8_t)(value & 0xffu);
    bytes[offset + 1u] = (uint8_t)(value >> 8u);
}

static void write_le32(uint8_t *bytes, size_t offset, uint32_t value) {
    for (size_t i = 0; i < 4u; i++) {
        bytes[offset + i] = (uint8_t)(value >> (8u * i));
    }
}

static void write_le64(uint8_t *bytes, size_t offset, uint64_t value) {
    for (size_t i = 0; i < 8u; i++) {
        bytes[offset + i] = (uint8_t)(value >> (8u * i));
    }
}

static uint32_t encode_movz(uint8_t rd, uint16_t imm16, unsigned shift, bool is_64_bit) {
    uint32_t sf = is_64_bit ? 0x80000000u : 0u;
    uint32_t hw = (uint32_t)(shift / 16u) & 0x3u;
    return sf | 0x52800000u | (hw << 21u) | ((uint32_t)imm16 << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_add_imm(uint8_t rd, uint8_t rn, uint16_t imm12, bool is_64_bit) {
    return (is_64_bit ? 0x91000000u : 0x11000000u) | ((uint32_t)(imm12 & 0xfffu) << 10u) |
           ((uint32_t)(rn & 0x1fu) << 5u) | (uint32_t)(rd & 0x1fu);
}

static uint32_t encode_ldr_unsigned(uint8_t rt, uint8_t rn, uint16_t imm12, bool is_64_bit) {
    uint32_t base = is_64_bit ? 0xf9400000u : 0xb9400000u;
    return base | ((uint32_t)(imm12 & 0xfffu) << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) |
           (uint32_t)(rt & 0x1fu);
}

static uint32_t encode_str_unsigned(uint8_t rt, uint8_t rn, uint16_t imm12, bool is_64_bit) {
    uint32_t base = is_64_bit ? 0xf9000000u : 0xb9000000u;
    return base | ((uint32_t)(imm12 & 0xfffu) << 10u) | ((uint32_t)(rn & 0x1fu) << 5u) |
           (uint32_t)(rt & 0x1fu);
}

static uint32_t encode_b(int64_t offset) {
    return 0x14000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
}

static uint32_t encode_bl(int64_t offset) {
    return 0x94000000u | ((uint32_t)((offset / 4) & 0x03ffffffu));
}

static uint32_t encode_ret(uint8_t rn) {
    return 0xd65f0000u | ((uint32_t)(rn & 0x1fu) << 5u);
}

static void put_op(uint8_t *bytes, size_t offset, uint32_t opcode) {
    write_le32(bytes, offset, opcode);
}

static void fixture_init(ElfFixture *fixture, uint64_t entry, uint16_t phnum) {
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

static void fixture_set_ph(ElfFixture *fixture, uint16_t index, uint32_t type, uint32_t flags, uint64_t offset,
                           uint64_t vaddr, uint64_t filesz, uint64_t memsz, uint64_t align,
                           const uint8_t *data) {
    size_t ph = ELF64_EHDR_SIZE + (size_t)index * ELF64_PHDR_SIZE;
    write_le32(fixture->bytes, ph + ELF_P_TYPE, type);
    write_le32(fixture->bytes, ph + ELF_P_FLAGS, flags);
    write_le64(fixture->bytes, ph + ELF_P_OFFSET, offset);
    write_le64(fixture->bytes, ph + ELF_P_VADDR, vaddr);
    write_le64(fixture->bytes, ph + ELF_P_FILESZ, filesz);
    write_le64(fixture->bytes, ph + ELF_P_MEMSZ, memsz);
    write_le64(fixture->bytes, ph + ELF_P_ALIGN, align);
    if (filesz > 0 && data != NULL && offset <= FIXTURE_CAPACITY && filesz <= FIXTURE_CAPACITY - offset) {
        memcpy(&fixture->bytes[offset], data, (size_t)filesz);
    }
    if (offset <= FIXTURE_CAPACITY && filesz <= FIXTURE_CAPACITY - offset && offset + filesz > fixture->size) {
        fixture->size = (size_t)(offset + filesz);
    }
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

static void write_fixture_or_die(const char *path, const ElfFixture *fixture) {
    write_file_or_die(path, fixture->bytes, fixture->size);
}

static void init_emulator_or_die(Emulator *emu) {
    char error[256];
    if (!emulator_init(emu, error, sizeof(error))) {
        fprintf(stderr, "emulator_init failed: %s\n", error);
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

static size_t read_stream(FILE *file, uint8_t *out, size_t capacity) {
    fflush(file);
    if (fseek(file, 0, SEEK_SET) != 0) {
        perror("fseek");
        exit(1);
    }
    return fread(out, 1, capacity, file);
}

static void make_code_bytes(uint8_t *bytes, const uint32_t *ops, size_t op_count) {
    for (size_t i = 0; i < op_count; i++) {
        put_op(bytes, i * 4u, ops[i]);
    }
}

static void write_minimal_elf(const char *path, uint64_t vaddr, uint64_t entry, const uint32_t *ops, size_t op_count) {
    uint8_t code[256];
    ElfFixture fixture;
    make_code_bytes(code, ops, op_count);
    fixture_init(&fixture, entry, 1);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, vaddr,
                   (uint64_t)(op_count * 4u), (uint64_t)(op_count * 4u), 1, code);
    write_fixture_or_die(path, &fixture);
}

static bool load_program_path(Emulator *emu, const char *path, EmuLoadedProgram *program, char *error,
                              size_t error_size) {
    memset(error, 0, error_size);
    return emulator_load_program(emu, path, program, error, error_size);
}

static void test_header_detection_and_validation(void) {
    /* TC-V08-HDR-001 through TC-V08-HDR-017. */
    char error[512];
    Emulator emu;
    EmuLoadedProgram program;
    const uint32_t raw_ops[] = {encode_movz(0, 1, 0, true), OP_HLT_0};
    uint8_t raw_bytes[sizeof(raw_ops)];
    ElfFixture fixture;

    make_code_bytes(raw_bytes, raw_ops, sizeof(raw_ops) / sizeof(raw_ops[0]));
    write_file_or_die("tests/v0_8/tmp/raw.bin", raw_bytes, sizeof(raw_bytes));
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/raw.bin", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_RAW);
    EXPECT_U64_EQ(emu.cpu.pc, EMU_LOAD_ADDRESS);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 1u);
    emulator_free(&emu);

    for (size_t len = 1; len <= 3; len++) {
        const uint8_t tiny[] = {EMU_ELF_MAGIC0, EMU_ELF_MAGIC1, EMU_ELF_MAGIC2};
        char path[64];
        snprintf(path, sizeof(path), "tests/v0_8/tmp/tiny_%zu.bin", len);
        write_file_or_die(path, tiny, len);
        init_emulator_or_die(&emu);
        EXPECT_TRUE(load_program_path(&emu, path, &program, error, sizeof(error)));
        EXPECT_U64_EQ(program.format, EMU_PROGRAM_RAW);
        emulator_free(&emu);
    }

    const uint32_t elf_ops[] = {encode_movz(0, 7, 0, true), OP_HLT_0};
    write_minimal_elf("tests/v0_8/tmp/minimal.elf", 0x4000, 0x4000, elf_ops, 2);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/minimal.elf", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.format, EMU_PROGRAM_ELF64);
    EXPECT_U64_EQ(emu.cpu.pc, 0x4000u);
    emulator_free(&emu);

    const uint8_t truncated_elf[] = {0x7f, 'E', 'L', 'F', EMU_ELF_CLASS_64};
    write_file_or_die("tests/v0_8/tmp/truncated.elf", truncated_elf, sizeof(truncated_elf));
    init_emulator_or_die(&emu);
    EXPECT_FALSE(load_program_path(&emu, "tests/v0_8/tmp/truncated.elf", &program, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "ELF header is truncated");
    emulator_free(&emu);

    fixture_init(&fixture, 0x1000, 1);
    uint8_t code[8];
    make_code_bytes(code, elf_ops, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, sizeof(code),
                   sizeof(code), 1, code);

    struct HeaderPatch {
        const char *path;
        size_t offset;
        uint64_t value;
        size_t width;
        const char *needle;
    } patches[] = {
        {"tests/v0_8/tmp/elf32.elf", ELF_EI_CLASS, 1, 1, "ELF class"},
        {"tests/v0_8/tmp/bigendian.elf", ELF_EI_DATA, 2, 1, "data encoding"},
        {"tests/v0_8/tmp/ident_version.elf", ELF_EI_VERSION, 2, 1, "e_ident ELF version"},
        {"tests/v0_8/tmp/e_version.elf", ELF_E_VERSION, 2, 4, "ELF version"},
        {"tests/v0_8/tmp/wrong_machine.elf", ELF_E_MACHINE, 62, 2, "expected AArch64"},
        {"tests/v0_8/tmp/relocatable.elf", ELF_E_TYPE, 1, 2, "unsupported ELF type"},
        {"tests/v0_8/tmp/core.elf", ELF_E_TYPE, 4, 2, "unsupported ELF type"},
        {"tests/v0_8/tmp/dyn.elf", ELF_E_TYPE, EMU_ELF_ET_DYN, 2, "ET_DYN"},
        {"tests/v0_8/tmp/ehsize_small.elf", ELF_E_EHSIZE, 63, 2, "ELF header size"},
        {"tests/v0_8/tmp/ehsize_large.elf", ELF_E_EHSIZE, 9000, 2, "ELF header size"},
        {"tests/v0_8/tmp/phoff_zero.elf", ELF_E_PHOFF, 0, 8, "program-header table offset"},
        {"tests/v0_8/tmp/phnum_zero.elf", ELF_E_PHNUM, 0, 2, "no program headers"},
        {"tests/v0_8/tmp/phentsize_bad.elf", ELF_E_PHENTSIZE, 55, 2, "program-header entry size"},
        {"tests/v0_8/tmp/phoff_outside.elf", ELF_E_PHOFF, 0x2000, 8, "program-header table outside file"},
        {"tests/v0_8/tmp/phoff_overflow.elf", ELF_E_PHOFF, UINT64_MAX - 8u, 8, "program-header table outside file"},
    };

    for (size_t i = 0; i < sizeof(patches) / sizeof(patches[0]); i++) {
        ElfFixture patched = fixture;
        if (patches[i].width == 1u) {
            patched.bytes[patches[i].offset] = (uint8_t)patches[i].value;
        } else if (patches[i].width == 2u) {
            write_le16(patched.bytes, patches[i].offset, (uint16_t)patches[i].value);
        } else if (patches[i].width == 4u) {
            write_le32(patched.bytes, patches[i].offset, (uint32_t)patches[i].value);
        } else {
            write_le64(patched.bytes, patches[i].offset, patches[i].value);
        }
        write_fixture_or_die(patches[i].path, &patched);
        init_emulator_or_die(&emu);
        EXPECT_FALSE(load_program_path(&emu, patches[i].path, &program, error, sizeof(error)));
        EXPECT_STR_CONTAINS(error, patches[i].needle);
        emulator_free(&emu);
    }
}

static void test_program_headers_and_segments(void) {
    /* TC-V08-PH-001 through TC-V08-PH-017 and TC-V08-SEG-001 through TC-V08-SEG-010. */
    char error[512];
    Emulator emu;
    EmuLoadedProgram program;
    ElfFixture fixture;
    uint8_t text[64];
    uint8_t data[] = {'D', 'A', 'T', 'A', 0x00, 0x7f};
    const uint32_t ops[] = {encode_movz(0, 7, 0, true), OP_HLT_0};

    make_code_bytes(text, ops, 2);
    fixture_init(&fixture, 0x1000, 3);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_NOTE, 0, 0, 0, 0, 0, 1, NULL);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, 8, 8, 0, text);
    fixture_set_ph(&fixture, 2, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_W, 0x180, 0x2000, sizeof(data), 16, 1,
                   data);
    write_fixture_or_die("tests/v0_8/tmp/two_segments.elf", &fixture);

    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/two_segments.elf", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.segment_count, 2u);
    EXPECT_U64_EQ(program.segments[0].vaddr, 0x1000u);
    EXPECT_U64_EQ(program.segments[0].flags, EMU_ELF_PF_R | EMU_ELF_PF_X);
    EXPECT_U64_EQ(program.segments[1].vaddr, 0x2000u);
    EXPECT_U64_EQ(program.segments[1].file_size, sizeof(data));
    EXPECT_U64_EQ(program.segments[1].mem_size, 16u);
    EXPECT_U64_EQ(program.segments[1].flags, EMU_ELF_PF_R | EMU_ELF_PF_W);
    EXPECT_MEM_EQ(&emu.memory.bytes[0x1000], text, 8);
    EXPECT_MEM_EQ(&emu.memory.bytes[0x2000], data, sizeof(data));
    for (size_t i = sizeof(data); i < 16u; i++) {
        EXPECT_U64_EQ(emu.memory.bytes[0x2000u + i], 0u);
    }
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 7u);
    emulator_free(&emu);

    struct PhFailure {
        const char *path;
        uint32_t type;
        uint64_t offset;
        uint64_t vaddr;
        uint64_t filesz;
        uint64_t memsz;
        const char *needle;
    } failures[] = {
        {"tests/v0_8/tmp/interp.elf", EMU_ELF_PT_INTERP, 0x100, 0x1000, 8, 8, "PT_INTERP"},
        {"tests/v0_8/tmp/filesz_gt_memsz.elf", EMU_ELF_PT_LOAD, 0x100, 0x1000, 16, 8, "filesz exceeds memsz"},
        {"tests/v0_8/tmp/off_outside.elf", EMU_ELF_PT_LOAD, 0x7000, 0x1000, 4, 4, "file range outside file"},
        {"tests/v0_8/tmp/off_overflow.elf", EMU_ELF_PT_LOAD, UINT64_MAX - 4u, 0x1000, 16, 16, "file range outside file"},
        {"tests/v0_8/tmp/mem_outside.elf", EMU_ELF_PT_LOAD, 0x100, EMU_MEMORY_SIZE - 2u, 4, 4, "memory range"},
        {"tests/v0_8/tmp/mem_overflow.elf", EMU_ELF_PT_LOAD, 0x100, UINT64_MAX - 8u, 4, 32, "memory range"},
    };
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); i++) {
        fixture_init(&fixture, failures[i].vaddr, 1);
        fixture_set_ph(&fixture, 0, failures[i].type, EMU_ELF_PF_R | EMU_ELF_PF_X, failures[i].offset,
                       failures[i].vaddr, failures[i].filesz, failures[i].memsz, 1, text);
        if (failures[i].offset > FIXTURE_CAPACITY || failures[i].filesz > FIXTURE_CAPACITY - failures[i].offset) {
            fixture.size = 0x200;
        }
        write_fixture_or_die(failures[i].path, &fixture);
        init_emulator_or_die(&emu);
        EXPECT_FALSE(load_program_path(&emu, failures[i].path, &program, error, sizeof(error)));
        EXPECT_STR_CONTAINS(error, failures[i].needle);
        emulator_free(&emu);
    }

    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, 8, 0x100, 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_W, 0x200, 0x1080, sizeof(data),
                   sizeof(data), 1, data);
    write_fixture_or_die("tests/v0_8/tmp/overlap.elf", &fixture);
    init_emulator_or_die(&emu);
    EXPECT_FALSE(load_program_path(&emu, "tests/v0_8/tmp/overlap.elf", &program, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "overlapping PT_LOAD");
    emulator_free(&emu);

    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, 8, 0x100, 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_W, 0x200, 0x1100, sizeof(data),
                   sizeof(data), 1, data);
    write_fixture_or_die("tests/v0_8/tmp/adjacent.elf", &fixture);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/adjacent.elf", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.segment_count, 2u);
    emulator_free(&emu);

    memset(text, 0xa5, sizeof(text));
    make_code_bytes(text, ops, 2);
    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, 8, 8, 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_W, 0x180, 0x3000, 0, 0, 1, NULL);
    write_fixture_or_die("tests/v0_8/tmp/zero_size.elf", &fixture);
    init_emulator_or_die(&emu);
    memset(&emu.memory.bytes[0x3000], 0xcc, 16);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/zero_size.elf", &program, error, sizeof(error)));
    for (size_t i = 0; i < 16u; i++) {
        EXPECT_U64_EQ(emu.memory.bytes[0x3000u + i], 0xccu);
    }
    emulator_free(&emu);

    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, 8, 8, 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_W, 0x180, 0x4000, 0, 32, 1, NULL);
    write_fixture_or_die("tests/v0_8/tmp/pure_bss.elf", &fixture);
    init_emulator_or_die(&emu);
    memset(&emu.memory.bytes[0x4000], 0xdd, 32);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/pure_bss.elf", &program, error, sizeof(error)));
    for (size_t i = 0; i < 32u; i++) {
        EXPECT_U64_EQ(emu.memory.bytes[0x4000u + i], 0u);
    }
    emulator_free(&emu);

    uint8_t last_byte = 0x5a;
    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, 8, 8, 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R, 0x180, EMU_MEMORY_SIZE - 1u, 1, 1, 1,
                   &last_byte);
    write_fixture_or_die("tests/v0_8/tmp/final_byte.elf", &fixture);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/final_byte.elf", &program, error, sizeof(error)));
    EXPECT_U64_EQ(emu.memory.bytes[EMU_MEMORY_SIZE - 1u], 0x5au);
    emulator_free(&emu);

    uint8_t large[256];
    memset(large, 0xee, sizeof(large));
    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, 8, 8, 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R, 0x200, EMU_MEMORY_SIZE - sizeof(large),
                   sizeof(large), sizeof(large), 1, large);
    write_fixture_or_die("tests/v0_8/tmp/near_max.elf", &fixture);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/near_max.elf", &program, error, sizeof(error)));
    EXPECT_MEM_EQ(&emu.memory.bytes[EMU_MEMORY_SIZE - sizeof(large)], large, sizeof(large));
    emulator_free(&emu);

    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, 8, 8, 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R, 0x200, EMU_MEMORY_SIZE - 1u, 2, 2, 1, large);
    write_fixture_or_die("tests/v0_8/tmp/one_past.elf", &fixture);
    init_emulator_or_die(&emu);
    EXPECT_FALSE(load_program_path(&emu, "tests/v0_8/tmp/one_past.elf", &program, error, sizeof(error)));
    EXPECT_STR_CONTAINS(error, "memory range");
    emulator_free(&emu);
}

static void test_entry_stack_and_execution(void) {
    /* TC-V08-ENTRY-001 through TC-V08-ENTRY-010 and TC-V08-EXEC-001 through TC-V08-EXEC-012. */
    char error[512];
    uint8_t stream_buffer[64];
    Emulator emu;
    EmuLoadedProgram program;
    ElfFixture fixture;
    uint8_t text[256];
    const uint32_t entry_offset_ops[] = {encode_movz(0, 99, 0, true), OP_HLT_0, OP_NOP, OP_NOP,
                                         encode_movz(0, 42, 0, true), OP_HLT_0};

    write_minimal_elf("tests/v0_8/tmp/entry_offset.elf", 0x4000, 0x4010, entry_offset_ops,
                      sizeof(entry_offset_ops) / sizeof(entry_offset_ops[0]));
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/entry_offset.elf", &program, error, sizeof(error)));
    EXPECT_U64_EQ(emu.cpu.pc, 0x4010u);
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    EXPECT_U64_EQ(program.stack_pointer, EMU_MEMORY_SIZE);
    for (size_t i = 0; i < 32u; i++) {
        EXPECT_U64_EQ(emu.memory.bytes[EMU_MEMORY_SIZE - 32u + i], 0u);
    }
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 42u);
    emulator_free(&emu);

    const uint32_t hlt_only[] = {OP_HLT_0};
    write_minimal_elf("tests/v0_8/tmp/final_instruction.elf", 0x5000, 0x5000, hlt_only, 1);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/final_instruction.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_step(&emu, error, sizeof(error)), EMU_HALTED);
    emulator_free(&emu);

    const uint32_t simple_ops[] = {encode_movz(0, 1, 0, true), OP_HLT_0};
    uint8_t simple_code[8];
    make_code_bytes(simple_code, simple_ops, 2);
    struct EntryFailure {
        const char *path;
        uint64_t entry;
        const char *needle;
    } entry_failures[] = {
        {"tests/v0_8/tmp/entry_outside.elf", 0x8000, "entry point"},
        {"tests/v0_8/tmp/entry_misaligned.elf", 0x1002, "4-byte aligned"},
    };
    for (size_t i = 0; i < sizeof(entry_failures) / sizeof(entry_failures[0]); i++) {
        fixture_init(&fixture, entry_failures[i].entry, 1);
        fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, 8, 8, 1,
                       simple_code);
        write_fixture_or_die(entry_failures[i].path, &fixture);
        init_emulator_or_die(&emu);
        EXPECT_FALSE(load_program_path(&emu, entry_failures[i].path, &program, error, sizeof(error)));
        EXPECT_STR_CONTAINS(error, entry_failures[i].needle);
        emulator_free(&emu);
    }

    fixture_init(&fixture, 0x2000, 1);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_W, 0x100, 0x2000, 8, 8, 1,
                   simple_code);
    write_fixture_or_die("tests/v0_8/tmp/entry_non_exec.elf", &fixture);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/entry_non_exec.elf", &program, error, sizeof(error)));
    EXPECT_U64_EQ(program.segments[0].flags, EMU_ELF_PF_R | EMU_ELF_PF_W);
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 1u);
    emulator_free(&emu);

    uint8_t top_text[4];
    make_code_bytes(top_text, hlt_only, 1);
    fixture_init(&fixture, EMU_MEMORY_SIZE - 4u, 1);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, EMU_MEMORY_SIZE - 4u, 4, 4,
                   1, top_text);
    write_fixture_or_die("tests/v0_8/tmp/stack_overlap_policy.elf", &fixture);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/stack_overlap_policy.elf", &program, error, sizeof(error)));
    EXPECT_U64_EQ(emu.cpu.sp, EMU_MEMORY_SIZE);
    EXPECT_STATUS(emulator_step(&emu, error, sizeof(error)), EMU_HALTED);
    emulator_free(&emu);

    const char hello[] = "ELF!";
    const uint32_t write_exit_ops[] = {
        encode_movz(0, 1, 0, true),
        encode_movz(1, 0x2000, 0, true),
        encode_movz(2, (uint16_t)(sizeof(hello) - 1u), 0, true),
        encode_movz(8, EMU_SYSCALL_WRITE, 0, true),
        OP_SVC_0,
        encode_movz(0, 0, 0, true),
        encode_movz(8, EMU_SYSCALL_EXIT, 0, true),
        OP_SVC_0,
    };
    make_code_bytes(text, write_exit_ops, sizeof(write_exit_ops) / sizeof(write_exit_ops[0]));
    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, sizeof(write_exit_ops),
                   sizeof(write_exit_ops), 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_W, 0x200, 0x2000, sizeof(hello) - 1u,
                   sizeof(hello) - 1u, 1, (const uint8_t *)hello);
    write_fixture_or_die("tests/v0_8/tmp/write_stdout.elf", &fixture);
    FILE *out = tmpfile_or_die();
    FILE *err = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    emu.stderr_stream = err;
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/write_stdout.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_TRUE(emu.guest_exited);
    EXPECT_U64_EQ(emu.guest_exit_code, 0u);
    EXPECT_U64_EQ(read_stream(out, stream_buffer, sizeof(stream_buffer)), sizeof(hello) - 1u);
    EXPECT_MEM_EQ(stream_buffer, hello, sizeof(hello) - 1u);
    EXPECT_U64_EQ(read_stream(err, stream_buffer, sizeof(stream_buffer)), 0u);
    fclose(out);
    fclose(err);
    emulator_free(&emu);

    const uint32_t stderr_ops[] = {
        encode_movz(0, 2, 0, true), encode_movz(1, 0x2000, 0, true),
        encode_movz(2, 3, 0, true), encode_movz(8, EMU_SYSCALL_WRITE, 0, true), OP_SVC_0, OP_HLT_0,
    };
    make_code_bytes(text, stderr_ops, sizeof(stderr_ops) / sizeof(stderr_ops[0]));
    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, sizeof(stderr_ops),
                   sizeof(stderr_ops), 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_W, 0x200, 0x2000, 3, 3, 1,
                   (const uint8_t *)"ERR");
    write_fixture_or_die("tests/v0_8/tmp/write_stderr.elf", &fixture);
    out = tmpfile_or_die();
    err = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    emu.stderr_stream = err;
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/write_stderr.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(read_stream(out, stream_buffer, sizeof(stream_buffer)), 0u);
    EXPECT_U64_EQ(read_stream(err, stream_buffer, sizeof(stream_buffer)), 3u);
    EXPECT_MEM_EQ(stream_buffer, "ERR", 3);
    fclose(out);
    fclose(err);
    emulator_free(&emu);

    const uint32_t bss_ops[] = {
        encode_movz(0, 0x4b4f, 0, true), /* bytes: O K */
        encode_movz(1, 0x3000, 0, true),
        encode_str_unsigned(0, 1, 0, true),
        encode_movz(0, 1, 0, true),
        encode_movz(1, 0x3000, 0, true),
        encode_movz(2, 2, 0, true),
        encode_movz(8, EMU_SYSCALL_WRITE, 0, true),
        OP_SVC_0,
        OP_HLT_0,
    };
    make_code_bytes(text, bss_ops, sizeof(bss_ops) / sizeof(bss_ops[0]));
    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, sizeof(bss_ops),
                   sizeof(bss_ops), 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_W, 0x200, 0x3000, 0, 8, 1, NULL);
    write_fixture_or_die("tests/v0_8/tmp/write_bss.elf", &fixture);
    out = tmpfile_or_die();
    init_emulator_or_die(&emu);
    emu.stdout_stream = out;
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/write_bss.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(read_stream(out, stream_buffer, sizeof(stream_buffer)), 2u);
    EXPECT_MEM_EQ(stream_buffer, "OK", 2);
    fclose(out);
    emulator_free(&emu);

    uint64_t value = 0x1234u;
    uint8_t value_bytes[8];
    write_le64(value_bytes, 0, value);
    const uint32_t load_data_ops[] = {encode_movz(1, 0x2000, 0, true), encode_ldr_unsigned(0, 1, 0, true), OP_HLT_0};
    make_code_bytes(text, load_data_ops, sizeof(load_data_ops) / sizeof(load_data_ops[0]));
    fixture_init(&fixture, 0x1000, 2);
    fixture_set_ph(&fixture, 0, EMU_ELF_PT_LOAD, EMU_ELF_PF_R | EMU_ELF_PF_X, 0x100, 0x1000, sizeof(load_data_ops),
                   sizeof(load_data_ops), 1, text);
    fixture_set_ph(&fixture, 1, EMU_ELF_PT_LOAD, EMU_ELF_PF_R, 0x200, 0x2000, 8, 8, 1, value_bytes);
    write_fixture_or_die("tests/v0_8/tmp/read_data.elf", &fixture);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/read_data.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), value);
    emulator_free(&emu);

    const uint32_t call_ops[] = {
        encode_bl(12), encode_movz(1, 99, 0, true), OP_HLT_0,
        encode_movz(0, 55, 0, true), encode_ret(30),
    };
    write_minimal_elf("tests/v0_8/tmp/call.elf", 0x4000, 0x4000, call_ops, sizeof(call_ops) / sizeof(call_ops[0]));
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/call.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 55u);
    emulator_free(&emu);

    const uint32_t branch_ops[] = {encode_movz(0, 3, 0, true), encode_b(8), encode_movz(0, 99, 0, true),
                                   encode_add_imm(0, 0, 4, true), OP_HLT_0};
    write_minimal_elf("tests/v0_8/tmp/branch.elf", 0x4000, 0x4000, branch_ops,
                      sizeof(branch_ops) / sizeof(branch_ops[0]));
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/branch.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), 7u);
    emulator_free(&emu);

    const uint32_t loop_ops[] = {encode_b(0)};
    write_minimal_elf("tests/v0_8/tmp/infinite.elf", 0x4000, 0x4000, loop_ops, 1);
    init_emulator_or_die(&emu);
    emu.instruction_limit = 8;
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/infinite.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "instruction limit");
    EXPECT_STR_CONTAINS(error, "pc=0x0000000000004000");
    emulator_free(&emu);

    const uint32_t bad_ops[] = {OP_UNSUPPORTED};
    write_minimal_elf("tests/v0_8/tmp/unsupported.elf", 0x4000, 0x4000, bad_ops, 1);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/unsupported.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "pc=0x0000000000004000");
    EXPECT_STR_CONTAINS(error, "opcode=0xffffffff");
    emulator_free(&emu);

    const uint32_t svc1_ops[] = {OP_SVC_1};
    write_minimal_elf("tests/v0_8/tmp/svc1.elf", 0x4000, 0x4000, svc1_ops, 1);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/svc1.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_ERROR);
    EXPECT_STR_CONTAINS(error, "svc immediate");
    emulator_free(&emu);

    const uint32_t unknown_syscall_ops[] = {encode_movz(8, 999, 0, true), OP_SVC_0, OP_HLT_0};
    write_minimal_elf("tests/v0_8/tmp/unknown_syscall.elf", 0x4000, 0x4000, unknown_syscall_ops, 3);
    init_emulator_or_die(&emu);
    EXPECT_TRUE(load_program_path(&emu, "tests/v0_8/tmp/unknown_syscall.elf", &program, error, sizeof(error)));
    EXPECT_STATUS(emulator_run(&emu, error, sizeof(error)), EMU_HALTED);
    EXPECT_U64_EQ(cpu_read_register(&emu.cpu, 0), (uint64_t)EMU_SYSCALL_ENOSYS);
    emulator_free(&emu);
}

static void test_debugger_with_elf(void) {
    /* TC-V08-DBG-001 through TC-V08-DBG-008 through the public debugger API. */
    char error[512];
    Debugger debugger;
    const uint32_t ops[] = {encode_movz(0, 1, 0, true), encode_add_imm(0, 0, 2, true), OP_HLT_0};

    write_minimal_elf("tests/v0_8/tmp/debug.elf", 0x4000, 0x4000, ops, sizeof(ops) / sizeof(ops[0]));
    EXPECT_TRUE(debugger_init(&debugger, "tests/v0_8/tmp/debug.elf", error, sizeof(error)));
    EXPECT_U64_EQ(debugger.emu.cpu.pc, 0x4000u);
    EXPECT_TRUE(debugger_add_breakpoint(&debugger, 0x4000, error, sizeof(error)));
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(debugger.stopped_at_breakpoint);
    EXPECT_U64_EQ(debugger.emu.cpu.pc, 0x4000u);
    EXPECT_STATUS(debugger_step(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_U64_EQ(cpu_read_register(&debugger.emu.cpu, 0), 1u);
    EXPECT_U64_EQ(debugger.emu.cpu.pc, 0x4004u);
    EXPECT_TRUE(debugger_add_breakpoint(&debugger, 0x4008, error, sizeof(error)));
    EXPECT_STATUS(debugger_continue(&debugger, error, sizeof(error)), EMU_OK);
    EXPECT_TRUE(debugger.stopped_at_breakpoint);
    EXPECT_U64_EQ(debugger.emu.cpu.pc, 0x4008u);
    EXPECT_TRUE(debugger_add_breakpoint(&debugger, 0x8000, error, sizeof(error)));
    EXPECT_TRUE(debugger_reset(&debugger, error, sizeof(error)));
    EXPECT_U64_EQ(debugger.emu.cpu.pc, 0x4000u);
    debugger_free(&debugger);
}

int main(void) {
    if (system("mkdir -p tests/v0_8/tmp") != 0) {
        fprintf(stderr, "failed to create tests/v0_8/tmp\n");
        return 1;
    }

    test_header_detection_and_validation();
    test_program_headers_and_segments();
    test_entry_stack_and_execution();
    test_debugger_with_elf();

    if (tests_failed != 0) {
        fprintf(stderr, "v0.8 tests failed: %d/%d failed\n", tests_failed, tests_run);
        return 1;
    }
    printf("v0.8 tests passed (%d checks)\n", tests_run);
    return 0;
}