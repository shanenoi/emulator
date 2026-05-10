#include "emulator.h"

#include <ctype.h>
#include <errno.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DEBUGGER_LINE_SIZE 256u

static void debugger_clear_break_stop(Debugger *debugger) {
    debugger->stopped_at_breakpoint = false;
    debugger->stopped_breakpoint_address = 0;
}

static bool parse_u64_debug(const char *text, uint64_t *out) {
    char *end = NULL;
    errno = 0;
    unsigned long long value = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return false;
    }
    *out = (uint64_t)value;
    return true;
}

static char *trim_left(char *text) {
    while (*text != '\0' && isspace((unsigned char)*text)) {
        text++;
    }
    return text;
}

static void trim_right(char *text) {
    size_t length = strlen(text);
    while (length > 0 && isspace((unsigned char)text[length - 1u])) {
        text[length - 1u] = '\0';
        length--;
    }
}

static char *next_token(char **cursor) {
    char *start = trim_left(*cursor);
    if (*start == '\0') {
        *cursor = start;
        return NULL;
    }

    char *end = start;
    while (*end != '\0' && !isspace((unsigned char)*end)) {
        end++;
    }

    if (*end != '\0') {
        *end = '\0';
        end++;
    }
    *cursor = end;
    return start;
}

static bool has_extra_tokens(char **cursor) {
    return next_token(cursor) != NULL;
}

static bool require_no_extra_args(char **cursor, const char *usage, FILE *error_stream) {
    if (has_extra_tokens(cursor)) {
        fprintf(error_stream, "error: usage: %s\n", usage);
        return false;
    }
    return true;
}

static void consume_overlong_line(FILE *input) {
    int ch = 0;
    while ((ch = fgetc(input)) != '\n' && ch != EOF) {
        /* discard remaining characters */
    }
}

static void debugger_print_help(FILE *stream) {
    fprintf(stream, "commands:\n");
    fprintf(stream, "  help                         show this help\n");
    fprintf(stream, "  run | r                      reset and run from the loaded program start\n");
    fprintf(stream, "  step | s                     execute exactly one instruction\n");
    fprintf(stream, "  continue | c                 resume until breakpoint, halt, error, or limit\n");
    fprintf(stream, "  regs                         print registers\n");
    fprintf(stream, "  mem | x <address> <length>   dump memory\n");
    fprintf(stream, "  break | b <address>          add breakpoint\n");
    fprintf(stream, "  delete <id-or-address>       delete breakpoint\n");
    fprintf(stream, "  breakpoints                  list breakpoints\n");
    fprintf(stream, "  trace on|off                 toggle pc trace\n");
    fprintf(stream, "  quit | q                     exit debugger\n");
}

static bool dump_memory_debug(const Memory *memory, uint64_t address, uint64_t length, FILE *stream, char *error,
                              size_t error_size) {
    if (address > (uint64_t)memory->size || length > (uint64_t)memory->size ||
        address + length > (uint64_t)memory->size || address + length < address) {
        snprintf(error, error_size, "dump range out of bounds: address=0x%016" PRIx64 " length=0x%016" PRIx64
                 " memory_size=0x%zx", address, length, memory->size);
        return false;
    }

    fprintf(stream, "memory dump address=0x%016" PRIx64 " length=0x%016" PRIx64 "\n", address, length);
    for (uint64_t offset = 0; offset < length; offset += 16u) {
        fprintf(stream, "0x%016" PRIx64 ":", address + offset);
        uint64_t line_len = length - offset < 16u ? length - offset : 16u;
        for (uint64_t i = 0; i < line_len; i++) {
            fprintf(stream, " %02x", memory->bytes[address + offset + i]);
        }
        fprintf(stream, "\n");
    }
    return true;
}

static void debugger_print_stop(EmuStatus status, const char *error, FILE *output, FILE *error_stream) {
    if (status == EMU_HALTED) {
        fprintf(output, "halted\n");
    } else if (status == EMU_ERROR) {
        fprintf(error_stream, "error: %s\n", error);
    }
}

static void debugger_print_trace_line(const Debugger *debugger, FILE *stream) {
    uint32_t opcode = 0;
    char formatted[256];
    char error[256];

    if (!cpu_fetch(&debugger->emu.cpu, &debugger->emu.memory, &opcode, error, sizeof(error))) {
        fprintf(stream, "trace pc=0x%016" PRIx64 " <fetch-error: %s>\n", debugger->emu.cpu.pc, error);
        return;
    }

    (void)cpu_format_instruction(opcode, debugger->emu.cpu.pc, formatted, sizeof(formatted));
    fprintf(stream, "trace pc=0x%016" PRIx64 " %s\n", debugger->emu.cpu.pc, formatted);
}

bool debugger_init(Debugger *debugger, const char *path, char *error, size_t error_size) {
    memset(debugger, 0, sizeof(*debugger));
    debugger->path = path;
    if (!emulator_init(&debugger->emu, error, error_size)) {
        return false;
    }
    if (!load_raw_binary(&debugger->emu.memory, path, EMU_LOAD_ADDRESS, error, error_size)) {
        emulator_free(&debugger->emu);
        return false;
    }
    debugger->loaded = true;
    return true;
}

void debugger_free(Debugger *debugger) {
    emulator_free(&debugger->emu);
    debugger->loaded = false;
}

bool debugger_reset(Debugger *debugger, char *error, size_t error_size) {
    const char *path = debugger->path;
    bool trace_enabled = debugger->emu.trace_enabled;
    FILE *trace_stream = debugger->emu.trace_stream;
    DebugBreakpoint breakpoints[EMU_MAX_BREAKPOINTS];
    size_t breakpoint_count = debugger->breakpoint_count;

    memcpy(breakpoints, debugger->breakpoints, sizeof(breakpoints));
    emulator_free(&debugger->emu);
    memset(&debugger->emu, 0, sizeof(debugger->emu));

    if (!emulator_init(&debugger->emu, error, error_size)) {
        return false;
    }
    debugger->emu.trace_enabled = trace_enabled;
    debugger->emu.trace_stream = trace_stream;

    if (!load_raw_binary(&debugger->emu.memory, path, EMU_LOAD_ADDRESS, error, error_size)) {
        return false;
    }

    memcpy(debugger->breakpoints, breakpoints, sizeof(debugger->breakpoints));
    debugger->breakpoint_count = breakpoint_count;
    debugger->loaded = true;
    debugger_clear_break_stop(debugger);
    return true;
}

bool debugger_add_breakpoint(Debugger *debugger, uint64_t address, char *error, size_t error_size) {
    if ((address & 0x3ull) != 0) {
        snprintf(error, error_size, "breakpoint address must be 4-byte aligned: 0x%016" PRIx64, address);
        return false;
    }
    if (address > (uint64_t)debugger->emu.memory.size || debugger->emu.memory.size - (size_t)address < sizeof(uint32_t)) {
        snprintf(error, error_size, "breakpoint address outside memory: 0x%016" PRIx64, address);
        return false;
    }
    for (size_t i = 0; i < debugger->breakpoint_count; i++) {
        if (debugger->breakpoints[i].enabled && debugger->breakpoints[i].address == address) {
            snprintf(error, error_size, "breakpoint already exists at 0x%016" PRIx64, address);
            return false;
        }
    }
    if (debugger->breakpoint_count >= EMU_MAX_BREAKPOINTS) {
        snprintf(error, error_size, "maximum breakpoints reached: %u", (unsigned)EMU_MAX_BREAKPOINTS);
        return false;
    }
    debugger->breakpoints[debugger->breakpoint_count].address = address;
    debugger->breakpoints[debugger->breakpoint_count].enabled = true;
    debugger->breakpoint_count++;
    return true;
}

bool debugger_delete_breakpoint(Debugger *debugger, uint64_t address_or_id, char *error, size_t error_size) {
    size_t index = debugger->breakpoint_count;
    if (address_or_id > 0 && address_or_id <= debugger->breakpoint_count) {
        index = (size_t)address_or_id - 1u;
    } else {
        for (size_t i = 0; i < debugger->breakpoint_count; i++) {
            if (debugger->breakpoints[i].address == address_or_id) {
                index = i;
                break;
            }
        }
    }

    if (index >= debugger->breakpoint_count) {
        snprintf(error, error_size, "breakpoint not found: 0x%016" PRIx64, address_or_id);
        return false;
    }

    for (size_t i = index; i + 1u < debugger->breakpoint_count; i++) {
        debugger->breakpoints[i] = debugger->breakpoints[i + 1u];
    }
    debugger->breakpoint_count--;
    return true;
}

bool debugger_has_breakpoint(const Debugger *debugger, uint64_t address) {
    for (size_t i = 0; i < debugger->breakpoint_count; i++) {
        if (debugger->breakpoints[i].enabled && debugger->breakpoints[i].address == address) {
            return true;
        }
    }
    return false;
}

void debugger_list_breakpoints(const Debugger *debugger, FILE *stream) {
    if (debugger->breakpoint_count == 0) {
        fprintf(stream, "no breakpoints\n");
        return;
    }
    for (size_t i = 0; i < debugger->breakpoint_count; i++) {
        fprintf(stream, "%zu: 0x%016" PRIx64 "%s\n", i + 1u, debugger->breakpoints[i].address,
                debugger->breakpoints[i].enabled ? "" : " disabled");
    }
}

EmuStatus debugger_step(Debugger *debugger, char *error, size_t error_size) {
    debugger_clear_break_stop(debugger);
    if (debugger->emu.cpu.halted) {
        snprintf(error, error_size, "program is already halted; use run to reset");
        return EMU_ERROR;
    }
    if (debugger->emu.cpu.instructions_executed >= debugger->emu.instruction_limit) {
        snprintf(error, error_size, "execution error: instruction limit reached: 0x%016" PRIx64,
                 debugger->emu.instruction_limit);
        return EMU_ERROR;
    }
    if (debugger->emu.trace_enabled) {
        FILE *stream = debugger->emu.trace_stream != NULL ? debugger->emu.trace_stream : stdout;
        debugger_print_trace_line(debugger, stream);
    }
    return cpu_step(&debugger->emu.cpu, &debugger->emu.memory, error, error_size);
}

EmuStatus debugger_continue(Debugger *debugger, char *error, size_t error_size) {
    bool skip_current_breakpoint = debugger->stopped_at_breakpoint;
    uint64_t skip_address = debugger->stopped_breakpoint_address;

    while (!debugger->emu.cpu.halted) {
        if (debugger->emu.cpu.instructions_executed >= debugger->emu.instruction_limit) {
            snprintf(error, error_size, "execution error: instruction limit reached: 0x%016" PRIx64,
                     debugger->emu.instruction_limit);
            debugger_clear_break_stop(debugger);
            return EMU_ERROR;
        }

        if (debugger_has_breakpoint(debugger, debugger->emu.cpu.pc) &&
            !(skip_current_breakpoint && debugger->emu.cpu.pc == skip_address)) {
            debugger->stopped_at_breakpoint = true;
            debugger->stopped_breakpoint_address = debugger->emu.cpu.pc;
            snprintf(error, error_size, "breakpoint hit at 0x%016" PRIx64, debugger->emu.cpu.pc);
            return EMU_OK;
        }
        skip_current_breakpoint = false;

        EmuStatus status = debugger_step(debugger, error, error_size);
        if (status != EMU_OK) {
            return status;
        }
    }
    return EMU_HALTED;
}

int debugger_repl(Debugger *debugger, FILE *input, FILE *output, FILE *error_stream) {
    char line[DEBUGGER_LINE_SIZE];
    char error[512];

    fprintf(output, "tiny-aarch64 debugger v0.5\n");
    fprintf(output, "loaded %s at 0x%016llx\n", debugger->path, (unsigned long long)EMU_LOAD_ADDRESS);

    while (true) {
        fprintf(output, "emu> ");
        fflush(output);

        if (fgets(line, sizeof(line), input) == NULL) {
            fprintf(output, "quit\n");
            return 0;
        }

        size_t line_length = strlen(line);
        if (line_length > 0 && line[line_length - 1u] != '\n' && !feof(input)) {
            consume_overlong_line(input);
            fprintf(error_stream, "error: input line too long; maximum is %u characters\n",
                    (unsigned)(DEBUGGER_LINE_SIZE - 2u));
            continue;
        }

        trim_right(line);
        char *cursor = line;
        char *command = next_token(&cursor);
        if (command == NULL || command[0] == '#') {
            continue;
        }

        if (strcmp(command, "quit") == 0 || strcmp(command, "q") == 0) {
            if (!require_no_extra_args(&cursor, "quit", error_stream)) {
                continue;
            }
            return 0;
        }
        if (strcmp(command, "help") == 0) {
            if (!require_no_extra_args(&cursor, "help", error_stream)) {
                continue;
            }
            debugger_print_help(output);
            continue;
        }
        if (strcmp(command, "regs") == 0) {
            if (!require_no_extra_args(&cursor, "regs", error_stream)) {
                continue;
            }
            cpu_dump(&debugger->emu.cpu, output);
            continue;
        }
        if (strcmp(command, "breakpoints") == 0) {
            if (!require_no_extra_args(&cursor, "breakpoints", error_stream)) {
                continue;
            }
            debugger_list_breakpoints(debugger, output);
            continue;
        }
        if (strcmp(command, "trace") == 0) {
            char *mode = next_token(&cursor);
            if (mode == NULL || has_extra_tokens(&cursor)) {
                fprintf(error_stream, "error: usage: trace on|off\n");
            } else if (strcmp(mode, "on") == 0) {
                debugger->emu.trace_enabled = true;
                debugger->emu.trace_stream = output;
                fprintf(output, "trace enabled\n");
            } else if (strcmp(mode, "off") == 0) {
                debugger->emu.trace_enabled = false;
                fprintf(output, "trace disabled\n");
            } else {
                fprintf(error_stream, "error: usage: trace on|off\n");
            }
            continue;
        }
        if (strcmp(command, "mem") == 0 || strcmp(command, "x") == 0) {
            char *address_text = next_token(&cursor);
            char *length_text = next_token(&cursor);
            uint64_t address = 0;
            uint64_t length = 0;
            if (address_text == NULL || length_text == NULL || has_extra_tokens(&cursor) ||
                !parse_u64_debug(address_text, &address) ||
                !parse_u64_debug(length_text, &length)) {
                fprintf(error_stream, "error: usage: mem <address> <length>\n");
                continue;
            }
            if (!dump_memory_debug(&debugger->emu.memory, address, length, output, error, sizeof(error))) {
                fprintf(error_stream, "error: %s\n", error);
            }
            continue;
        }
        if (strcmp(command, "break") == 0 || strcmp(command, "b") == 0) {
            char *address_text = next_token(&cursor);
            uint64_t address = 0;
            if (address_text == NULL || has_extra_tokens(&cursor) || !parse_u64_debug(address_text, &address)) {
                fprintf(error_stream, "error: usage: break <address>\n");
                continue;
            }
            if (debugger_add_breakpoint(debugger, address, error, sizeof(error))) {
                fprintf(output, "breakpoint %zu set at 0x%016" PRIx64 "\n", debugger->breakpoint_count, address);
            } else {
                fprintf(error_stream, "error: %s\n", error);
            }
            continue;
        }
        if (strcmp(command, "delete") == 0) {
            char *target_text = next_token(&cursor);
            uint64_t target = 0;
            if (target_text == NULL || has_extra_tokens(&cursor) || !parse_u64_debug(target_text, &target)) {
                fprintf(error_stream, "error: usage: delete <breakpoint-id-or-address>\n");
                continue;
            }
            if (debugger_delete_breakpoint(debugger, target, error, sizeof(error))) {
                fprintf(output, "breakpoint deleted\n");
            } else {
                fprintf(error_stream, "error: %s\n", error);
            }
            continue;
        }
        if (strcmp(command, "step") == 0 || strcmp(command, "s") == 0) {
            if (!require_no_extra_args(&cursor, "step", error_stream)) {
                continue;
            }
            EmuStatus status = debugger_step(debugger, error, sizeof(error));
            fprintf(output, "pc=0x%016" PRIx64 "\n", debugger->emu.cpu.pc);
            debugger_print_stop(status, error, output, error_stream);
            continue;
        }
        if (strcmp(command, "continue") == 0 || strcmp(command, "c") == 0) {
            if (!require_no_extra_args(&cursor, "continue", error_stream)) {
                continue;
            }
            EmuStatus status = debugger_continue(debugger, error, sizeof(error));
            if (status == EMU_OK && debugger->stopped_at_breakpoint) {
                fprintf(output, "%s\n", error);
            } else {
                debugger_print_stop(status, error, output, error_stream);
            }
            continue;
        }
        if (strcmp(command, "run") == 0 || strcmp(command, "r") == 0) {
            if (!require_no_extra_args(&cursor, "run", error_stream)) {
                continue;
            }
            if (!debugger_reset(debugger, error, sizeof(error))) {
                fprintf(error_stream, "error: %s\n", error);
                continue;
            }
            EmuStatus status = debugger_continue(debugger, error, sizeof(error));
            if (status == EMU_OK && debugger->stopped_at_breakpoint) {
                fprintf(output, "%s\n", error);
            } else {
                debugger_print_stop(status, error, output, error_stream);
            }
            continue;
        }

        fprintf(error_stream, "error: unknown command: %s\n", command);
    }
}
