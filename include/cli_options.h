#ifndef CLI_OPTIONS_H
#define CLI_OPTIONS_H

#include "emulator.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    bool has_exception_vector;
    uint64_t exception_vector;
    bool has_timer_interval;
    uint64_t timer_interval;
    bool queue_timer;
    bool has_interrupts_enabled;
    bool interrupts_enabled;
    bool kernel_enabled;
    bool kernel_boot_info;
    uint64_t kernel_tasks[EMU_TOY_KERNEL_MAX_TASKS];
    size_t kernel_task_count;
    const char *input_text;
    const char *input_file;
    bool has_screen_size;
    uint32_t screen_width;
    uint32_t screen_height;
    bool screen_dump;
    const char *screen_border;
    bool interactive;
    bool has_frames;
    uint64_t frames;
    bool has_fps;
    uint32_t fps;
    uint64_t instructions_per_frame;
    const char *quit_key;
} CliOptions;

void cli_print_usage(FILE *stream);
bool cli_parse_options(int argc, char **argv, int start, CliOptions *options, char *error, size_t error_size);
bool cli_apply_options(Emulator *emu, const CliOptions *options, char *error, size_t error_size);
bool cli_apply_input(Memory *memory, const CliOptions *options, char *error, size_t error_size);

#endif
