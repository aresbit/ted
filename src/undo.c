/**
 * undo.c - Undo/Redo stack implementation
 */

#include "ted.h"

void undo_init(undo_stack_t *stack) {
    stack->actions = SP_NULLPTR;
    stack->count = 0;
    stack->capacity = 0;
    stack->current = 0;
}

static void ensure_capacity(undo_stack_t *stack) {
    if (stack->count >= stack->capacity) {
        u32 new_cap = stack->capacity == 0 ? 16 : stack->capacity * 2;
        action_t *new_actions = sp_alloc(sizeof(action_t) * new_cap);

        for (u32 i = 0; i < stack->count; i++) {
            new_actions[i] = stack->actions[i];
        }

        if (stack->actions) {
            sp_free(stack->actions);
        }
        stack->actions = new_actions;
        stack->capacity = new_cap;
    }
}

void undo_push(undo_stack_t *stack, action_t *action) {
    ensure_capacity(stack);

    // If we're not at the end, truncate the stack
    if (stack->current < stack->count) {
        // Free actions that will be overwritten
        for (u32 i = stack->current; i < stack->count; i++) {
            if (stack->actions[i].type == ACTION_INSERT_LINE ||
                stack->actions[i].type == ACTION_DELETE_LINE) {
                if (stack->actions[i].text.data) {
                    sp_free((void*)stack->actions[i].text.data);
                }
            }
        }
        stack->count = stack->current;
    }

    stack->actions[stack->count] = *action;
    stack->count++;
    stack->current = stack->count;
}

action_t* undo_pop(undo_stack_t *stack) {
    if (stack->current == 0) return SP_NULLPTR;
    stack->current--;
    return &stack->actions[stack->current];
}

void undo_clear(undo_stack_t *stack) {
    // Free text data for line operations
    for (u32 i = 0; i < stack->count; i++) {
        if (stack->actions[i].type == ACTION_INSERT_LINE ||
            stack->actions[i].type == ACTION_DELETE_LINE) {
            if (stack->actions[i].text.data) {
                sp_free((void*)stack->actions[i].text.data);
            }
        }
    }
    if (stack->actions) {
        sp_free(stack->actions);
    }
    stack->actions = SP_NULLPTR;
    stack->count = 0;
    stack->capacity = 0;
    stack->current = 0;
}

void undo_record_insert(u32 row, u32 col, c8 c) {
    c8 buf[2] = {c, '\0'};
    action_t action = {
        .type = ACTION_INSERT,
        .row = row,
        .col = col,
        .text = sp_str(buf, 1),
        .old_text = sp_str_lit("")
    };
    undo_push(&E.undo, &action);
    undo_clear(&E.redo);
}

void undo_record_delete(u32 row, u32 col, c8 c) {
    c8 buf[2] = {c, '\0'};
    action_t action = {
        .type = ACTION_DELETE,
        .row = row,
        .col = col,
        .text = sp_str(buf, 1),
        .old_text = sp_str_lit("")
    };
    undo_push(&E.undo, &action);
    undo_clear(&E.redo);
}

void undo_record_insert_line(u32 row, sp_str_t text) {
    action_t action = {
        .type = ACTION_INSERT_LINE,
        .row = row,
        .col = 0,
        .text = sp_str_copy(text),
        .old_text = sp_str_lit("")
    };
    undo_push(&E.undo, &action);
    undo_clear(&E.redo);
}

void undo_record_delete_line(u32 row, sp_str_t text) {
    action_t action = {
        .type = ACTION_DELETE_LINE,
        .row = row,
        .col = 0,
        .text = sp_str_copy(text),
        .old_text = sp_str_lit("")
    };
    undo_push(&E.undo, &action);
    undo_clear(&E.redo);
}

void undo_perform(void) {
    action_t *action = undo_pop(&E.undo);
    if (!action) {
        editor_set_message("Nothing to undo");
        return;
    }

    // Create redo action
    action_t redo_action = *action;

    switch (action->type) {
        case ACTION_INSERT: {
            // Undo insert = delete the char
            buffer_delete_char_at(&E.buffer, action->row, action->col);
            E.cursor.row = action->row;
            E.cursor.col = action->col;
            redo_action.type = ACTION_DELETE;
            break;
        }
        case ACTION_DELETE: {
            // Undo delete = insert the char back
            c8 c = action->text.len > 0 ? action->text.data[0] : ' ';
            buffer_insert_char_at(&E.buffer, action->row, action->col, c);
            E.cursor.row = action->row;
            E.cursor.col = action->col + 1;
            redo_action.type = ACTION_INSERT;
            break;
        }
        case ACTION_INSERT_LINE: {
            // Undo insert line = delete the line
            buffer_delete_line(&E.buffer, action->row);
            E.cursor.row = action->row > 0 ? action->row - 1 : 0;
            E.cursor.col = 0;
            redo_action.type = ACTION_DELETE_LINE;
            break;
        }
        case ACTION_DELETE_LINE: {
            // Undo delete line = insert the line back
            buffer_insert_line(&E.buffer, action->row, action->text);
            E.cursor.row = action->row;
            E.cursor.col = 0;
            redo_action.type = ACTION_INSERT_LINE;
            break;
        }
    }

    // Push to redo stack
    ensure_capacity(&E.redo);
    E.redo.actions[E.redo.count] = redo_action;
    E.redo.count++;
    E.redo.current = E.redo.count;

    E.cursor.render_col = buffer_row_to_render(&E.buffer, E.cursor.row, E.cursor.col);
    editor_set_message("Undo");
}

void redo_perform(void) {
    action_t *action = undo_pop(&E.redo);
    if (!action) {
        editor_set_message("Nothing to redo");
        return;
    }

    // Create undo action
    action_t undo_action = *action;

    switch (action->type) {
        case ACTION_INSERT: {
            // Redo insert = insert the char
            buffer_insert_char_at(&E.buffer, action->row, action->col, action->text.data[0]);
            E.cursor.row = action->row;
            E.cursor.col = action->col + 1;
            undo_action.type = ACTION_DELETE;
            break;
        }
        case ACTION_DELETE: {
            // Redo delete = delete the char
            buffer_delete_char_at(&E.buffer, action->row, action->col);
            E.cursor.row = action->row;
            E.cursor.col = action->col;
            undo_action.type = ACTION_INSERT;
            break;
        }
        case ACTION_INSERT_LINE: {
            // Redo insert line = insert the line
            buffer_insert_line(&E.buffer, action->row, action->text);
            E.cursor.row = action->row;
            E.cursor.col = 0;
            undo_action.type = ACTION_DELETE_LINE;
            break;
        }
        case ACTION_DELETE_LINE: {
            // Redo delete line = delete the line
            buffer_delete_line(&E.buffer, action->row);
            E.cursor.row = action->row > 0 ? action->row - 1 : 0;
            E.cursor.col = 0;
            undo_action.type = ACTION_INSERT_LINE;
            break;
        }
    }

    // Push to undo stack
    ensure_capacity(&E.undo);
    E.undo.actions[E.undo.count] = undo_action;
    E.undo.count++;
    E.undo.current = E.undo.count;

    E.cursor.render_col = buffer_row_to_render(&E.buffer, E.cursor.row, E.cursor.col);
    editor_set_message("Redo");
}
