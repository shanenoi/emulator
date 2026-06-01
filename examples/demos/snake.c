#include "emulator_guest.h"

#define SNAKE_MAX_LEN 256u
#define MIN_PLAY_WIDTH 12u
#define MIN_PLAY_HEIGHT 8u

enum Direction {
    DIR_UP = 0,
    DIR_DOWN = 1,
    DIR_LEFT = 2,
    DIR_RIGHT = 3,
};

static uint8_t snake_x[SNAKE_MAX_LEN];
static uint8_t snake_y[SNAKE_MAX_LEN];
static uint32_t snake_len;
static uint32_t score;
static uint32_t food_x;
static uint32_t food_y;
static uint32_t screen_w;
static uint32_t screen_h;
static uint32_t play_w;
static uint32_t play_h;
static uint32_t origin_x;
static uint32_t origin_y;
static enum Direction direction;
static int game_over;
static int quit_requested;

static uint32_t clamp_min(uint32_t value, uint32_t min_value) {
    return value < min_value ? min_value : value;
}

static uint32_t board_right(void) {
    return origin_x + play_w + 1u;
}

static uint32_t board_bottom(void) {
    return origin_y + play_h + 1u;
}

static int same_cell(uint32_t ax, uint32_t ay, uint32_t bx, uint32_t by) {
    return ax == bx && ay == by;
}

static int snake_contains(uint32_t x, uint32_t y) {
    for (uint32_t i = 0; i < snake_len; i++) {
        if (same_cell(snake_x[i], snake_y[i], x, y)) {
            return 1;
        }
    }
    return 0;
}

static void place_food(void) {
    uint32_t cells = play_w * play_h;
    uint32_t start = emu_guest_random_range(cells);

    for (uint32_t n = 0; n < cells; n++) {
        uint32_t cell = (start + n) % cells;
        uint32_t x = (cell % play_w) + 1u;
        uint32_t y = (cell / play_w) + 1u;
        if (!snake_contains(x, y)) {
            food_x = x;
            food_y = y;
            return;
        }
    }

    food_x = 0u;
    food_y = 0u;
}

static void draw_horizontal_border(uint32_t y) {
    emu_guest_screen_putc_at(origin_x, y, '+');
    for (uint32_t x = 0; x < play_w; x++) {
        emu_guest_screen_putc_at(origin_x + x + 1u, y, '-');
    }
    emu_guest_screen_putc_at(board_right(), y, '+');
}

static void draw_border(void) {
    draw_horizontal_border(origin_y);
    for (uint32_t y = 0; y < play_h; y++) {
        emu_guest_screen_putc_at(origin_x, origin_y + y + 1u, '|');
        emu_guest_screen_putc_at(board_right(), origin_y + y + 1u, '|');
    }
    draw_horizontal_border(board_bottom());
}

static void draw_hud(void) {
    emu_guest_screen_puts_at(0u, 0u, "Snake score:");
    emu_guest_screen_set_cursor(12u, 0u);
    emu_guest_screen_put_u32_dec(score);
    emu_guest_screen_puts("  WASD/arrows move, Esc quits");
}

static void clear_play_area(void) {
    for (uint32_t y = 0; y < play_h; y++) {
        for (uint32_t x = 0; x < play_w; x++) {
            emu_guest_screen_putc_at(origin_x + x + 1u, origin_y + y + 1u, ' ');
        }
    }
}

static void draw_world(void) {
    emu_guest_screen_clear();
    draw_hud();
    draw_border();
    clear_play_area();

    if (food_x != 0u && food_y != 0u) {
        emu_guest_screen_putc_at(origin_x + food_x, origin_y + food_y, '*');
    }

    for (uint32_t i = snake_len; i > 0u; i--) {
        uint32_t index = i - 1u;
        char body = index == 0u ? '@' : 'o';
        emu_guest_screen_putc_at(origin_x + snake_x[index], origin_y + snake_y[index], body);
    }

    if (game_over) {
        uint32_t x = play_w > 18u ? origin_x + ((play_w - 18u) / 2u) + 1u : origin_x + 1u;
        uint32_t y = origin_y + (play_h / 2u) + 1u;
        emu_guest_screen_puts_at(x, y, "GAME OVER - Esc");
    }
}

static void set_direction(enum Direction next) {
    if ((direction == DIR_UP && next == DIR_DOWN) ||
        (direction == DIR_DOWN && next == DIR_UP) ||
        (direction == DIR_LEFT && next == DIR_RIGHT) ||
        (direction == DIR_RIGHT && next == DIR_LEFT)) {
        return;
    }
    direction = next;
}

static void handle_key(uint8_t key) {
    if (key == 'q' || key == 'Q' || key == EMU_GUEST_KEY_ESC) {
        quit_requested = 1;
        return;
    }
    if (game_over) {
        return;
    }
    if (key == 'w' || key == 'W' || key == EMU_GUEST_KEY_UP) {
        set_direction(DIR_UP);
    } else if (key == 's' || key == 'S' || key == EMU_GUEST_KEY_DOWN) {
        set_direction(DIR_DOWN);
    } else if (key == 'a' || key == 'A' || key == EMU_GUEST_KEY_LEFT) {
        set_direction(DIR_LEFT);
    } else if (key == 'd' || key == 'D' || key == EMU_GUEST_KEY_RIGHT) {
        set_direction(DIR_RIGHT);
    }
}

static void poll_keys(void) {
    while (emu_guest_key_available()) {
        handle_key(emu_guest_key_read());
    }
}

static void step_snake(void) {
    uint32_t head_x = snake_x[0];
    uint32_t head_y = snake_y[0];

    if (direction == DIR_UP) {
        head_y--;
    } else if (direction == DIR_DOWN) {
        head_y++;
    } else if (direction == DIR_LEFT) {
        head_x--;
    } else {
        head_x++;
    }

    if (head_x == 0u || head_y == 0u || head_x > play_w || head_y > play_h) {
        game_over = 1;
        return;
    }

    int eating = same_cell(head_x, head_y, food_x, food_y);
    uint32_t check_len = eating ? snake_len : snake_len - 1u;
    for (uint32_t i = 0; i < check_len; i++) {
        if (same_cell(snake_x[i], snake_y[i], head_x, head_y)) {
            game_over = 1;
            return;
        }
    }

    uint32_t new_len = snake_len + (eating && snake_len < SNAKE_MAX_LEN ? 1u : 0u);
    for (uint32_t i = new_len; i > 1u; i--) {
        snake_x[i - 1u] = snake_x[i - 2u];
        snake_y[i - 1u] = snake_y[i - 2u];
    }
    snake_x[0] = (uint8_t)head_x;
    snake_y[0] = (uint8_t)head_y;
    snake_len = new_len;

    if (eating) {
        score++;
        if (snake_len == SNAKE_MAX_LEN || snake_len == play_w * play_h) {
            game_over = 1;
        } else {
            place_food();
        }
    }
}

static void init_game(void) {
    screen_w = emu_guest_screen_width();
    screen_h = emu_guest_screen_height();
    play_w = screen_w > 2u ? screen_w - 2u : 1u;
    play_h = screen_h > 4u ? screen_h - 4u : 1u;
    play_w = clamp_min(play_w, 1u);
    play_h = clamp_min(play_h, 1u);
    if (play_w > 70u) {
        play_w = 70u;
    }
    if (play_h > 30u) {
        play_h = 30u;
    }
    origin_x = 0u;
    origin_y = 2u;

    snake_len = 3u;
    score = 0u;
    direction = DIR_RIGHT;
    game_over = 0;
    quit_requested = 0;

    uint32_t mid_x = play_w / 2u;
    uint32_t mid_y = play_h / 2u;
    if (mid_x < 4u) {
        mid_x = 4u;
    }
    if (mid_x > play_w) {
        mid_x = play_w;
    }
    if (mid_y < 1u) {
        mid_y = 1u;
    }

    snake_x[0] = (uint8_t)mid_x;
    snake_y[0] = (uint8_t)mid_y;
    snake_x[1] = (uint8_t)(mid_x - 1u);
    snake_y[1] = (uint8_t)mid_y;
    snake_x[2] = (uint8_t)(mid_x - 2u);
    snake_y[2] = (uint8_t)mid_y;

    emu_guest_random_seed((screen_w << 16u) ^ screen_h ^ 0x5eed1234u);
    place_food();
}

int main(void) {
    init_game();
    draw_world();

    for (;;) {
        emu_guest_wait_frame();
        poll_keys();
        if (quit_requested) {
            emu_guest_exit(0);
        }
        if (!game_over) {
            step_snake();
        }
        draw_world();
    }
}
