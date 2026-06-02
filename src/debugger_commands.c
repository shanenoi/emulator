#include "debugger_commands.h"

#include "emu_format.h"
#include "emu_util.h"

#include <ctype.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#define DEBUGGER_LINE_SIZE 256u

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
    fprintf(stream, "  exception                    print the current exception context\n");
    fprintf(stream, "  kernel                       print toy-kernel task state\n");
    fprintf(stream, "  tasks                        alias for kernel task table\n");
    fprintf(stream, "  mem | x <address> <length>   dump memory\n");
    fprintf(stream, "  maps                         list mapped memory ranges\n");
    fprintf(stream, "  map <address>                show mapping containing one address\n");
    fprintf(stream, "  break | b <address>          add breakpoint\n");
    fprintf(stream, "  delete <id-or-address>       delete breakpoint\n");
    fprintf(stream, "  breakpoints                  list breakpoints\n");
    fprintf(stream, "  trace on|off                 toggle pc trace\n");
    fprintf(stream, "  quit | q                     exit debugger\n");
}

static const char *debugger_toy_task_state_name(EmuToyTaskState state) {
    switch (state) {
    case EMU_TOY_TASK_EMPTY:
        return "empty";
    case EMU_TOY_TASK_READY:
        return "ready";
    case EMU_TOY_TASK_RUNNING:
        return "running";
    case EMU_TOY_TASK_BLOCKED:
        return "blocked";
    case EMU_TOY_TASK_EXITED:
        return "exited";
    case EMU_TOY_TASK_FAULTED:
        return "faulted";
    }
    return "unknown";
}

static void debugger_print_kernel_context(const Debugger *debugger, FILE *stream) {
    const EmuToyKernel *kernel = emulator_get_toy_kernel(&debugger->emu);
    fprintf(stream, "toy_kernel_enabled = %s\n", kernel->enabled ? "yes" : "no");
    if (!kernel->enabled) {
        return;
    }
    fprintf(stream, "tasks_started = %s\n", kernel->tasks_started ? "yes" : "no");
    fprintf(stream, "completed = %s\n", kernel->completed ? "yes" : "no");
    fprintf(stream, "current_task = %zu\n", kernel->current_task);
    fprintf(stream, "task_count = %zu\n", kernel->task_count);
    fprintf(stream, "descriptor_table = 0x%016" PRIx64 "\n", kernel->descriptor_table_address);
    fprintf(stream, "service_trap = 0x%03x\n", EMU_TOY_KERNEL_TRAP_SERVICE);
    fprintf(stream, "supported_services = 0x%016" PRIx64 "\n", (uint64_t)EMU_TOY_SERVICE_SUPPORTED_MASK);
    fprintf(stream, "service_calls = 0x%016" PRIx64 "\n", kernel->service_calls);
    fprintf(stream, "last_service = id=0x%016" PRIx64 " status=%" PRId64 "\n", kernel->last_service_id,
            kernel->last_service_status);
    fprintf(stream, "mailbox_ops = sends=0x%016" PRIx64 " recvs=0x%016" PRIx64 "\n", kernel->mailbox_sends,
            kernel->mailbox_recvs);
    fprintf(stream, "next_task_id = 0x%016" PRIx64 "\n", kernel->next_task_id);
    fprintf(stream, "timer_ticks = 0x%016" PRIx64 "\n", kernel->timer_ticks);
    fprintf(stream, "timer_schedules = 0x%016" PRIx64 "\n", kernel->timer_schedules);
    fprintf(stream, "panic = %s code=0x%016" PRIx64 "\n", kernel->panic ? "yes" : "no", kernel->panic_code);
    for (size_t i = 0; i < kernel->task_count; i++) {
        const EmuToyTask *task = &kernel->tasks[i];
        fprintf(stream,
                "task[%zu] state=%s id=0x%016" PRIx64 " name=%s origin=%s entry=0x%016" PRIx64 " pc=0x%016" PRIx64
                " sp=0x%016" PRIx64 " wake=0x%016" PRIx64
                " exit=0x%016" PRIx64 " switches=0x%016" PRIx64
                " mailbox=%zu/%u fault=0x%02x/0x%016" PRIx64 "\n",
                i, debugger_toy_task_state_name(task->state), task->task_id, task->name[0] == '\0' ? "-" : task->name,
                task->guest_created ? "guest" : "host", task->entry,
                task->pc, task->sp, task->wake_tick, task->exit_code, task->switch_count, task->mailbox_count,
                EMU_TOY_KERNEL_MAILBOX_SLOTS, (unsigned)task->fault_cause, task->fault_address);
    }
}

static void debugger_print_stop(const Debugger *debugger, EmuStatus status, const char *error, FILE *output,
                                FILE *error_stream) {
    if (status == EMU_HALTED) {
        if (debugger->emu.guest_exited) {
            fprintf(output, "exited status=%u\n", (unsigned)debugger->emu.guest_exit_code);
        } else {
            fprintf(output, "halted\n");
        }
    } else if (status == EMU_ERROR) {
        fprintf(error_stream, "error: %s\n", error);
    }
}


static void debugger_print_exception_context(const Debugger *debugger, FILE *stream) {
    const EmuExceptionContext *context = emulator_get_exception_context(&debugger->emu);
    fprintf(stream, "exception_active = %s\n", debugger->emu.exceptions.active ? "yes" : "no");
    fprintf(stream, "vector_configured = %s\n", debugger->emu.exceptions.vector_configured ? "yes" : "no");
    fprintf(stream, "vector_base = 0x%016" PRIx64 "\n", debugger->emu.exceptions.vector_base);
    fprintf(stream, "interrupts_enabled = %s\n", debugger->emu.exceptions.interrupts_enabled ? "yes" : "no");
    fprintf(stream, "pending_timer_interrupt = %s\n", debugger->emu.exceptions.pending_timer_interrupt ? "yes" : "no");
    fprintf(stream, "cause = 0x%02x\n", (unsigned)context->cause);
    fprintf(stream, "fault_address = 0x%016" PRIx64 "\n", context->fault_address);
    fprintf(stream, "interrupted_pc = 0x%016" PRIx64 "\n", context->interrupted_pc);
    fprintf(stream, "resume_pc = 0x%016" PRIx64 "\n", context->resume_pc);
    fprintf(stream, "saved_nzcv = %u%u%u%u\n", context->flags.n ? 1u : 0u, context->flags.z ? 1u : 0u,
            context->flags.c ? 1u : 0u, context->flags.v ? 1u : 0u);
    fprintf(stream, "depth = %u\n", context->depth);
}

int debugger_repl(Debugger *debugger, FILE *input, FILE *output, FILE *error_stream) {
    char line[DEBUGGER_LINE_SIZE];
    char error[512];

    fprintf(output, "tiny-aarch64 debugger v0.5\n");
    fprintf(output, "loaded %s at 0x%016" PRIx64 "\n", debugger->path, debugger->emu.cpu.pc);

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
        if (strcmp(command, "exception") == 0) {
            if (!require_no_extra_args(&cursor, "exception", error_stream)) {
                continue;
            }
            debugger_print_exception_context(debugger, output);
            continue;
        }
        if (strcmp(command, "kernel") == 0 || strcmp(command, "tasks") == 0) {
            if (!require_no_extra_args(&cursor, strcmp(command, "kernel") == 0 ? "kernel" : "tasks", error_stream)) {
                continue;
            }
            debugger_print_kernel_context(debugger, output);
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
        if (strcmp(command, "maps") == 0) {
            if (!require_no_extra_args(&cursor, "maps", error_stream)) {
                continue;
            }
            memory_print_mappings(&debugger->emu.memory, output);
            continue;
        }
        if (strcmp(command, "map") == 0) {
            char *address_text = next_token(&cursor);
            uint64_t address = 0;
            if (address_text == NULL || has_extra_tokens(&cursor) || !emu_parse_u64_strict(address_text, &address)) {
                fprintf(error_stream, "error: usage: map <address>\n");
                continue;
            }
            const EmuMemoryMapping *mapping = memory_find_mapping(&debugger->emu.memory, address);
            if (mapping == NULL) {
                const EmuDeviceRange *device = memory_find_device(&debugger->emu.memory, address);
                if (device != NULL) {
                    char perms[4];
                    memory_format_permissions(device->permissions, perms, sizeof(perms));
                    fprintf(output,
                            "address 0x%016" PRIx64 " is in %s device 0x%016" PRIx64 "-0x%016" PRIx64
                            " name=%s\n",
                            address, perms, device->start, device->start + device->size,
                            device->name[0] == '\0' ? "device" : device->name);
                } else {
                    EmuMemoryMapping guard;
                    if (memory_find_stack_guard(&debugger->emu.memory, address, &guard)) {
                        fprintf(output,
                                "address 0x%016" PRIx64 " is in --- guard 0x%016" PRIx64 "-0x%016" PRIx64
                                " name=%s\n",
                                address, guard.start, guard.start + guard.size, guard.name);
                    } else {
                        fprintf(output, "address 0x%016" PRIx64 " is unmapped\n", address);
                    }
                }
            } else {
                char perms[4];
                memory_format_permissions(mapping->permissions, perms, sizeof(perms));
                fprintf(output,
                        "address 0x%016" PRIx64 " is in %s mapping 0x%016" PRIx64 "-0x%016" PRIx64
                        " name=%s\n",
                        address, perms, mapping->start, mapping->start + mapping->size,
                        mapping->name[0] == '\0' ? "mapping" : mapping->name);
            }
            continue;
        }
        if (strcmp(command, "mem") == 0 || strcmp(command, "x") == 0) {
            char *address_text = next_token(&cursor);
            char *length_text = next_token(&cursor);
            uint64_t address = 0;
            uint64_t length = 0;
            if (address_text == NULL || length_text == NULL || has_extra_tokens(&cursor) ||
                !emu_parse_u64_strict(address_text, &address) ||
                !emu_parse_u64_strict(length_text, &length)) {
                fprintf(error_stream, "error: usage: mem <address> <length>\n");
                continue;
            }
            if (!emu_format_memory_dump(&debugger->emu.memory, address, length, output, error, sizeof(error))) {
                fprintf(error_stream, "error: %s\n", error);
            }
            continue;
        }
        if (strcmp(command, "break") == 0 || strcmp(command, "b") == 0) {
            char *address_text = next_token(&cursor);
            uint64_t address = 0;
            if (address_text == NULL || has_extra_tokens(&cursor) || !emu_parse_u64_strict(address_text, &address)) {
                fprintf(error_stream, "error: usage: break <address>\n");
                continue;
            }
            if (address > (uint64_t)debugger->emu.memory.size ||
                debugger->emu.memory.size - (size_t)address < sizeof(uint32_t)) {
                fprintf(error_stream, "error: breakpoint address outside memory: 0x%016" PRIx64 "\n", address);
                continue;
            }
            if (address != 0 &&
                !memory_check_execute(&debugger->emu.memory, address, sizeof(uint32_t), error, sizeof(error))) {
                char cause[512];
                snprintf(cause, sizeof(cause), "%s", error);
                fprintf(error_stream, "error: breakpoint address is not executable: 0x%016" PRIx64 " (%.300s)\n",
                        address, cause);
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
            if (target_text == NULL || has_extra_tokens(&cursor) || !emu_parse_u64_strict(target_text, &target)) {
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
            debugger_print_stop(debugger, status, error, output, error_stream);
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
                debugger_print_stop(debugger, status, error, output, error_stream);
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
                debugger_print_stop(debugger, status, error, output, error_stream);
            }
            continue;
        }

        fprintf(error_stream, "error: unknown command: %s\n", command);
    }
}
