#ifndef EMULATOR_H
#define EMULATOR_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define EMU_MEMORY_SIZE (1024u * 1024u)
#define EMU_LOAD_ADDRESS 0x1000ull
#define EMU_DEFAULT_INSTRUCTION_LIMIT 1000000ull

typedef enum {
    EMU_OK = 0,
    EMU_ERROR = 1,
    EMU_HALTED = 2,
} EmuStatus;

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
} Emulator;

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
EmuStatus cpu_step(Cpu *cpu, const Memory *memory, char *error, size_t error_size);
void cpu_dump(const Cpu *cpu, FILE *stream);

bool load_raw_binary(Memory *memory, const char *path, uint64_t load_address, char *error, size_t error_size);

bool emulator_init(Emulator *emu, char *error, size_t error_size);
void emulator_free(Emulator *emu);
EmuStatus emulator_run(Emulator *emu, char *error, size_t error_size);

#endif