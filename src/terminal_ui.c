#define _POSIX_C_SOURCE 200809L

#include "terminal_ui.h"

#include "emulator.h"

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/select.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static void print_repeated(FILE *stream, const char *text, uint32_t count) {
    for (uint32_t i = 0; i < count; i++) {
        fputs(text, stream);
    }
}

void terminal_render_screen_dump(const Memory *memory, const char *border, FILE *stream) {
    uint32_t width = memory_terminal_width(memory);
    uint32_t height = memory_terminal_height(memory);
    const uint8_t *cells = memory_terminal_cells(memory);
    const char *style = border != NULL ? border : "unicode";

    if (strcmp(style, "none") != 0) {
        if (strcmp(style, "ascii") == 0) {
            fputc('+', stream);
            print_repeated(stream, "-", width);
            fputs("+\n", stream);
        } else {
            fputs("┌", stream);
            print_repeated(stream, "─", width);
            fputs("┐\n", stream);
        }
    }

    for (uint32_t y = 0; y < height; y++) {
        if (strcmp(style, "none") != 0) {
            fputs(strcmp(style, "ascii") == 0 ? "|" : "│", stream);
        }
        for (uint32_t x = 0; x < width; x++) {
            fputc((int)cells[(size_t)y * width + x], stream);
        }
        if (strcmp(style, "none") != 0) {
            fputs(strcmp(style, "ascii") == 0 ? "|" : "│", stream);
        }
        fputc('\n', stream);
    }

    if (strcmp(style, "none") != 0) {
        if (strcmp(style, "ascii") == 0) {
            fputc('+', stream);
            print_repeated(stream, "-", width);
            fputs("+\n", stream);
        } else {
            fputs("└", stream);
            print_repeated(stream, "─", width);
            fputs("┘\n", stream);
        }
    }
}

typedef struct {
    struct termios original_termios;
    int original_flags;
    bool active;
} InteractiveTerminalState;

static void interactive_terminal_restore(InteractiveTerminalState *state) {
    if (!state->active) {
        return;
    }
    (void)tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios);
    (void)fcntl(STDIN_FILENO, F_SETFL, state->original_flags);
    state->active = false;
}

static bool interactive_terminal_enter(InteractiveTerminalState *state, char *error, size_t error_size) {
    memset(state, 0, sizeof(*state));
    if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
        snprintf(error, error_size, "--interactive requires stdin and stdout to be TTYs");
        return false;
    }
    if (tcgetattr(STDIN_FILENO, &state->original_termios) != 0) {
        snprintf(error, error_size, "failed to read terminal settings: %s", strerror(errno));
        return false;
    }
    state->original_flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (state->original_flags < 0) {
        snprintf(error, error_size, "failed to read terminal flags: %s", strerror(errno));
        return false;
    }

    struct termios raw = state->original_termios;
    raw.c_lflag &= (tcflag_t)~(ICANON | ECHO);
    raw.c_cc[VMIN] = 0;
    raw.c_cc[VTIME] = 0;
    if (tcsetattr(STDIN_FILENO, TCSANOW, &raw) != 0) {
        snprintf(error, error_size, "failed to enable raw terminal mode: %s", strerror(errno));
        return false;
    }
    if (fcntl(STDIN_FILENO, F_SETFL, state->original_flags | O_NONBLOCK) != 0) {
        (void)tcsetattr(STDIN_FILENO, TCSANOW, &state->original_termios);
        snprintf(error, error_size, "failed to enable nonblocking terminal input: %s", strerror(errno));
        return false;
    }
    state->active = true;
    return true;
}

static bool poll_stdin_byte(uint8_t *out, char *error, size_t error_size) {
    fd_set readfds;
    FD_ZERO(&readfds);
    FD_SET(STDIN_FILENO, &readfds);
    struct timeval timeout = {0, 0};
    int ready = select(STDIN_FILENO + 1, &readfds, NULL, NULL, &timeout);
    if (ready < 0) {
        if (errno == EINTR) {
            return false;
        }
        snprintf(error, error_size, "failed to poll terminal input: %s", strerror(errno));
        return false;
    }
    if (ready == 0 || !FD_ISSET(STDIN_FILENO, &readfds)) {
        return false;
    }
    ssize_t n = read(STDIN_FILENO, out, 1);
    if (n == 1) {
        return true;
    }
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
        snprintf(error, error_size, "failed to read terminal input: %s", strerror(errno));
    }
    return false;
}

typedef struct {
    uint8_t escape_buffer[3];
    size_t escape_count;
} InteractiveInputNormalizer;

static bool interactive_normalize_byte(InteractiveInputNormalizer *normalizer, uint8_t byte, const CliOptions *options,
                                       uint8_t *out, bool *has_output, bool *host_quit) {
    *has_output = false;
    *host_quit = false;

    if (normalizer->escape_count > 0) {
        normalizer->escape_buffer[normalizer->escape_count++] = byte;
        if (normalizer->escape_count == 2 && normalizer->escape_buffer[1] != '[') {
            if (strcmp(options->quit_key, "esc") == 0 && normalizer->escape_buffer[0] == EMU_KEY_ESC) {
                *host_quit = true;
                normalizer->escape_count = 0;
                return true;
            }
            *out = normalizer->escape_buffer[0];
            *has_output = true;
            normalizer->escape_count = 0;
            return true;
        }
        if (normalizer->escape_count < 3) {
            return true;
        }
        switch (normalizer->escape_buffer[2]) {
        case 'A':
            *out = EMU_KEY_UP;
            *has_output = true;
            break;
        case 'B':
            *out = EMU_KEY_DOWN;
            *has_output = true;
            break;
        case 'D':
            *out = EMU_KEY_LEFT;
            *has_output = true;
            break;
        case 'C':
            *out = EMU_KEY_RIGHT;
            *has_output = true;
            break;
        default:
            if (strcmp(options->quit_key, "esc") == 0) {
                *host_quit = true;
            } else {
                *out = normalizer->escape_buffer[0];
                *has_output = true;
            }
            break;
        }
        normalizer->escape_count = 0;
        return true;
    }

    if (byte == 3 && strcmp(options->quit_key, "ctrl-c") == 0) {
        *host_quit = true;
        return true;
    }
    if (byte == EMU_KEY_ESC) {
        normalizer->escape_buffer[0] = byte;
        normalizer->escape_count = 1;
        return true;
    }
    if (byte == '\r') {
        byte = '\n';
    }
    *out = byte;
    *has_output = true;
    return true;
}

static void interactive_render_screen(Memory *memory, const CliOptions *options) {
    char ignored[128];
    fputs("\033[H\033[2J", stdout);
    terminal_render_screen_dump(memory, options->screen_border, stdout);
    fflush(stdout);
    (void)memory_write32(memory, EMU_DEVICE_TERMINAL_BASE + EMU_TERM_CONTROL_OFFSET, EMU_TERM_CONTROL_CLEAR_DIRTY,
                         ignored, sizeof(ignored));
}

static void sleep_frame(uint32_t fps) {
    struct timespec delay;
    delay.tv_sec = 0;
    delay.tv_nsec = (long)(1000000000ull / fps);
    while (nanosleep(&delay, &delay) != 0 && errno == EINTR) {
    }
}

EmuStatus terminal_run_interactive(Emulator *emu, const CliOptions *options, char *error, size_t error_size) {
    InteractiveTerminalState terminal;
    if (!interactive_terminal_enter(&terminal, error, error_size)) {
        return EMU_ERROR;
    }

    InteractiveInputNormalizer normalizer = {0};
    bool user_quit = false;
    EmuStatus status = EMU_OK;
    interactive_render_screen(&emu->memory, options);

    while (!user_quit) {
        uint8_t raw = 0;
        while (poll_stdin_byte(&raw, error, error_size)) {
            uint8_t normalized = 0;
            bool has_output = false;
            bool host_quit = false;
            if (!interactive_normalize_byte(&normalizer, raw, options, &normalized, &has_output, &host_quit)) {
                status = EMU_ERROR;
                goto done;
            }
            if (host_quit) {
                user_quit = true;
                break;
            }
            if (has_output) {
                (void)memory_keyboard_enqueue(&emu->memory, normalized);
            }
        }
        if (error[0] != '\0') {
            status = EMU_ERROR;
            goto done;
        }
        if (user_quit) {
            break;
        }

        for (uint64_t i = 0; i < options->instructions_per_frame; i++) {
            status = emulator_step(emu, error, error_size);
            if (status != EMU_OK) {
                goto done;
            }
        }
        memory_advance_frame(&emu->memory);

        if (memory_terminal_dirty(&emu->memory)) {
            interactive_render_screen(&emu->memory, options);
        }
        sleep_frame(options->fps);
    }
    status = EMU_HALTED;

done:
    if (memory_terminal_dirty(&emu->memory)) {
        interactive_render_screen(&emu->memory, options);
    }
    interactive_terminal_restore(&terminal);
    fputc('\n', stdout);
    if (user_quit && status == EMU_HALTED) {
        snprintf(error, error_size, "interactive quit requested");
    }
    return status;
}

EmuStatus terminal_run_frames(Emulator *emu, const CliOptions *options, char *error, size_t error_size) {
    EmuStatus status = EMU_OK;
    for (uint64_t frame = 0; frame < options->frames; frame++) {
        for (uint64_t i = 0; i < options->instructions_per_frame; i++) {
            if (emu->cpu.instructions_executed >= emu->instruction_limit) {
                snprintf(error, error_size, "instruction limit reached: 0x%016llx",
                         (unsigned long long)emu->instruction_limit);
                return EMU_ERROR;
            }
            status = emulator_step(emu, error, error_size);
            if (status != EMU_OK) {
                return status;
            }
        }
        memory_advance_frame(&emu->memory);
    }
    return EMU_HALTED;
}
