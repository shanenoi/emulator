#include "loader_internal.h"

#include <stdio.h>


static uint64_t choose_stack_top_below_mappings(const Memory *memory) {
    uint64_t stack_span = EMU_STACK_SIZE + (uint64_t)EMU_STACK_GUARD_PAGES * EMU_PAGE_SIZE;
    uint64_t stack_top = (uint64_t)memory->size;
    bool changed = true;

    while (changed) {
        changed = false;
        uint64_t stack_floor = stack_top >= stack_span ? stack_top - stack_span : 0;
        for (size_t i = 0; i < memory->mapping_count; i++) {
            const EmuMemoryMapping *mapping = &memory->mappings[i];
            uint64_t mapping_end = mapping->start + mapping->size;
            if (stack_floor < mapping_end && mapping->start < stack_top) {
                stack_top = loader_align_down_to_page(mapping->start);
                changed = true;
                break;
            }
        }
    }

    return stack_top;
}


bool loader_map_stack_for_program(Emulator *emu, EmuLoadedProgram *program, const char *prefix, char *error,
                                  size_t error_size) {
    program->stack_pointer = choose_stack_top_below_mappings(&emu->memory);
    if (!memory_map_stack(&emu->memory, program->stack_pointer, EMU_STACK_SIZE, error, error_size)) {
        char detail[512];
        snprintf(detail, sizeof(detail), "%s", error);
        snprintf(error, error_size, "%s: %s", prefix, detail);
        return false;
    }
    return true;
}
