#include "fuse_vault/secret_input.h"

#include <stdio.h>
#include <string.h>

#define DIRECTION_MIN_LENGTH 6u
#define KEYPAD_MIN_LENGTH 4u
#define WORD_LIST_SIZE 64u

_Static_assert(WORD_LIST_SIZE == 4u * 4u * 4u,
               "word picker requires three balanced four-way choices");

_Static_assert(FV_SECRET_DIRECTION_MAX <= FV_SECRET_ENCODING_SIZE - 5u,
               "direction sequence exceeds canonical encoding");
_Static_assert(FV_SECRET_KEYPAD_MAX <= FV_SECRET_ENCODING_SIZE - 5u,
               "keypad PIN exceeds canonical encoding");

static const char *const words[WORD_LIST_SIZE] = {
    "amber", "apple", "atlas", "baker", "beach", "birch", "bloom", "brave",
    "cedar", "charm", "cloud", "coral", "delta", "dream", "eagle", "ember",
    "fable", "field", "flame", "flora", "frost", "giant", "globe", "grape",
    "haven", "hazel", "honey", "ivory", "jolly", "karma", "lemon", "light",
    "lunar", "maple", "metal", "mint", "noble", "north", "ocean", "olive",
    "orbit", "pearl", "piano", "pixel", "prism", "queen", "quiet", "raven",
    "river", "robin", "solar", "spark", "stone", "storm", "tiger", "trail",
    "union", "valley", "vivid", "water", "whale", "willow", "world", "zebra",
};

static void secure_clear(void *data, size_t length) {
    volatile uint8_t *bytes = data;
    while (length-- > 0u) *bytes++ = 0u;
}

const char *fv_secret_method_name(fv_entry_method_t method) {
    static const char *const names[FV_ENTRY_METHOD_COUNT] = {
        "Number wheels", "Direction sequence", "Numeric keypad", "Word list",
    };
    return (unsigned)method < FV_ENTRY_METHOD_COUNT ? names[method] : "Unknown";
}

void fv_secret_entry_clear(fv_secret_entry_t *entry) {
    if (entry != NULL) secure_clear(entry, sizeof(*entry));
}

void fv_secret_entry_begin(fv_secret_entry_t *entry, fv_entry_method_t method) {
    if (entry == NULL) return;
    fv_secret_entry_clear(entry);
    entry->method = method;
    if (method == FV_ENTRY_METHOD_WORD_LIST) {
        memset(entry->state.word_list.words, FV_SECRET_WORD_EMPTY,
               sizeof(entry->state.word_list.words));
    }
}

static fv_secret_event_result_t handle_wheels(fv_wheel_entry_t *wheels,
                                               fv_event_t event) {
    if (event == FV_EVENT_LEFT) {
        wheels->selected = wheels->selected == 0u ? FV_SECRET_WHEEL_COUNT - 1u
                                                   : (uint8_t)(wheels->selected - 1u);
    } else if (event == FV_EVENT_RIGHT) {
        wheels->selected = (uint8_t)((wheels->selected + 1u) % FV_SECRET_WHEEL_COUNT);
    } else if (event == FV_EVENT_UP) {
        uint8_t *value = &wheels->values[wheels->selected];
        *value = (uint8_t)((*value + 1u) % FV_SECRET_WHEEL_VALUES);
    } else if (event == FV_EVENT_DOWN) {
        uint8_t *value = &wheels->values[wheels->selected];
        *value = *value == 0u ? FV_SECRET_WHEEL_VALUES - 1u : (uint8_t)(*value - 1u);
    } else if (event == FV_EVENT_SELECT) {
        return FV_SECRET_EVENT_COMPLETE;
    } else return FV_SECRET_EVENT_IGNORED;
    return FV_SECRET_EVENT_CHANGED;
}

static fv_secret_event_result_t handle_directions(fv_direction_entry_t *directions,
                                                   fv_event_t event) {
    uint8_t value;
    if (event == FV_EVENT_UP) value = 0u;
    else if (event == FV_EVENT_RIGHT) value = 1u;
    else if (event == FV_EVENT_DOWN) value = 2u;
    else if (event == FV_EVENT_LEFT) value = 3u;
    else if (event == FV_EVENT_BACK && directions->length > 0u) {
        directions->values[--directions->length] = 0u;
        return FV_SECRET_EVENT_CHANGED;
    } else if (event == FV_EVENT_SELECT && directions->length >= DIRECTION_MIN_LENGTH) {
        return FV_SECRET_EVENT_COMPLETE;
    } else return FV_SECRET_EVENT_IGNORED;
    if (directions->length < FV_SECRET_DIRECTION_MAX) {
        directions->values[directions->length++] = value;
    }
    return FV_SECRET_EVENT_CHANGED;
}

static uint8_t keypad_value(uint8_t cell) {
    static const uint8_t values[12] = {1u,2u,3u,10u,4u,5u,6u,0u,7u,8u,9u,11u};
    return values[cell];
}

static fv_secret_event_result_t handle_keypad(fv_keypad_entry_t *keypad,
                                               fv_event_t event) {
    uint8_t row = (uint8_t)(keypad->selected / 4u);
    uint8_t column = (uint8_t)(keypad->selected % 4u);
    if (event == FV_EVENT_LEFT) column = column == 0u ? 3u : (uint8_t)(column - 1u);
    else if (event == FV_EVENT_RIGHT) column = (uint8_t)((column + 1u) % 4u);
    else if (event == FV_EVENT_UP) row = row == 0u ? 2u : (uint8_t)(row - 1u);
    else if (event == FV_EVENT_DOWN) row = (uint8_t)((row + 1u) % 3u);
    else if (event == FV_EVENT_BACK && keypad->length > 0u) {
        keypad->digits[--keypad->length] = 0u;
        return FV_SECRET_EVENT_CHANGED;
    } else if (event == FV_EVENT_SELECT) {
        const uint8_t value = keypad_value(keypad->selected);
        if (value == 10u && keypad->length > 0u) keypad->digits[--keypad->length] = 0u;
        else if (value == 11u && keypad->length >= KEYPAD_MIN_LENGTH) return FV_SECRET_EVENT_COMPLETE;
        else if (value < 10u && keypad->length < FV_SECRET_KEYPAD_MAX) keypad->digits[keypad->length++] = value;
        return FV_SECRET_EVENT_CHANGED;
    } else return FV_SECRET_EVENT_IGNORED;
    keypad->selected = (uint8_t)(row * 4u + column);
    return FV_SECRET_EVENT_CHANGED;
}

static bool words_complete(const fv_word_entry_t *word_list) {
    for (unsigned i = 0u; i < FV_SECRET_WORD_COUNT; ++i) {
        if (word_list->words[i] >= WORD_LIST_SIZE) return false;
    }
    return true;
}

static fv_secret_event_result_t handle_words(fv_word_entry_t *word_list,
                                              fv_event_t event) {
    if (event == FV_EVENT_BACK) {
        if (word_list->depth > 0u) {
            --word_list->depth;
            word_list->prefix /= 4u;
        } else if (!word_list->reviewing && words_complete(word_list)) {
            /* Cancel an edit without discarding the previously selected word. */
            word_list->reviewing = true;
        } else if (word_list->reviewing || word_list->selected > 0u) {
            if (word_list->reviewing) word_list->selected = FV_SECRET_WORD_COUNT - 1u;
            else --word_list->selected;
            word_list->words[word_list->selected] = FV_SECRET_WORD_EMPTY;
            word_list->reviewing = false;
        } else return FV_SECRET_EVENT_IGNORED;
        return FV_SECRET_EVENT_CHANGED;
    }
    if (event == FV_EVENT_SELECT) {
        return word_list->reviewing && words_complete(word_list)
            ? FV_SECRET_EVENT_COMPLETE : FV_SECRET_EVENT_IGNORED;
    }
    uint8_t choice;
    if (event == FV_EVENT_UP) choice = 0u;
    else if (event == FV_EVENT_RIGHT) choice = 1u;
    else if (event == FV_EVENT_DOWN) choice = 2u;
    else if (event == FV_EVENT_LEFT) choice = 3u;
    else return FV_SECRET_EVENT_IGNORED;

    if (word_list->reviewing) {
        word_list->selected = choice;
        word_list->reviewing = false;
    } else if (word_list->depth < 2u) {
        word_list->prefix = (uint8_t)(word_list->prefix * 4u + choice);
        ++word_list->depth;
    } else {
        word_list->words[word_list->selected] =
            (uint8_t)(word_list->prefix * 4u + choice);
        word_list->prefix = 0u;
        word_list->depth = 0u;
        if (words_complete(word_list)) word_list->reviewing = true;
        else {
            for (uint8_t i = 0u; i < FV_SECRET_WORD_COUNT; ++i) {
                if (word_list->words[i] == FV_SECRET_WORD_EMPTY) {
                    word_list->selected = i;
                    break;
                }
            }
        }
    }
    return FV_SECRET_EVENT_CHANGED;
}

fv_secret_event_result_t fv_secret_entry_handle(fv_secret_entry_t *entry,
                                                 fv_event_t event) {
    if (entry == NULL) return FV_SECRET_EVENT_IGNORED;
    switch (entry->method) {
        case FV_ENTRY_METHOD_WHEELS: return handle_wheels(&entry->state.wheels, event);
        case FV_ENTRY_METHOD_DIRECTIONS: return handle_directions(&entry->state.directions, event);
        case FV_ENTRY_METHOD_KEYPAD: return handle_keypad(&entry->state.keypad, event);
        case FV_ENTRY_METHOD_WORD_LIST: return handle_words(&entry->state.word_list, event);
        case FV_ENTRY_METHOD_COUNT: break;
    }
    return FV_SECRET_EVENT_IGNORED;
}

bool fv_secret_entry_encode(const fv_secret_entry_t *entry,
                            fv_secret_encoding_t *encoding) {
    if (entry == NULL || encoding == NULL || (unsigned)entry->method >= FV_ENTRY_METHOD_COUNT) return false;
    fv_secret_method_t secret_method;
    if (!fv_entry_method_to_secret_method(entry->method, &secret_method)) return false;
    memset(encoding, 0, sizeof(*encoding));
    encoding->bytes[0] = 0x46u;
    encoding->bytes[1] = 0x56u;
    encoding->bytes[2] = 0x02u; /* fixed-capacity modular encoding */
    size_t length = 0u;
    const uint8_t *values = NULL;
    switch (entry->method) {
        case FV_ENTRY_METHOD_WHEELS: length = FV_SECRET_WHEEL_COUNT; values = entry->state.wheels.values; break;
        case FV_ENTRY_METHOD_DIRECTIONS: length = entry->state.directions.length; values = entry->state.directions.values; break;
        case FV_ENTRY_METHOD_KEYPAD: length = entry->state.keypad.length; values = entry->state.keypad.digits; break;
        case FV_ENTRY_METHOD_WORD_LIST:
            if (!words_complete(&entry->state.word_list)) return false;
            length = FV_SECRET_WORD_COUNT;
            values = entry->state.word_list.words;
            break;
        case FV_ENTRY_METHOD_COUNT: return false;
    }
    encoding->bytes[3] = (uint8_t)secret_method;
    if (length > FV_SECRET_ENCODING_SIZE - 5u) return false;
    encoding->bytes[4] = (uint8_t)length;
    memcpy(encoding->bytes + 5u, values, length);
    return true;
}

static void keypad_row(const fv_keypad_entry_t *keypad, unsigned row, char *output) {
    static const char *const labels[12] = {"1","2","3","DEL","4","5","6","0","7","8","9","OK"};
    size_t used = 0u;
    for (unsigned column = 0u; column < 4u && used < FV_UI_TEXT_CAPACITY; ++column) {
        const unsigned cell = row * 4u + column;
        const int written = snprintf(output + used, FV_UI_TEXT_CAPACITY - used,
            cell == keypad->selected ? "[%s]" : " %s ", labels[cell]);
        if (written > 0) used += (size_t)written;
    }
    if (row == 0u && used < FV_UI_TEXT_CAPACITY) {
        (void)snprintf(output + used, FV_UI_TEXT_CAPACITY - used, " N%u",
                       (unsigned)keypad->length);
    }
}

void fv_secret_entry_render(const fv_secret_entry_t *entry, fv_ui_view_t *view) {
    if (entry == NULL || view == NULL) return;
    memset(view->lines, 0, sizeof(view->lines));
    view->direction_icons = false;
    view->word_picker = false;
    view->secret_controls = true;
    view->select_enabled = true;
    snprintf(view->back_action, sizeof(view->back_action), "Back");
    snprintf(view->select_action, sizeof(view->select_action), "Done");
    switch (entry->method) {
        case FV_ENTRY_METHOD_WHEELS: {
            const fv_wheel_entry_t *w = &entry->state.wheels;
            snprintf(view->lines[0], FV_UI_TEXT_CAPACITY,
                w->selected == 0u ? "[%02u]  %02u   %02u" : w->selected == 1u ? " %02u  [%02u]  %02u" : " %02u   %02u  [%02u]",
                (unsigned)w->values[0], (unsigned)w->values[1], (unsigned)w->values[2]);
            snprintf(view->lines[2], FV_UI_TEXT_CAPACITY, "\003\001 Slot  \004\002 Value");
            break;
        }
        case FV_ENTRY_METHOD_DIRECTIONS: {
            const fv_direction_entry_t *d = &entry->state.directions;
            view->direction_icons = true;
            view->select_enabled = d->length >= DIRECTION_MIN_LENGTH;
            snprintf(view->back_action, sizeof(view->back_action),
                     d->length > 0u ? "Delete" : "Back");
            snprintf(view->lines[0], FV_UI_TEXT_CAPACITY, "%u/%u arrows  Min %u",
                     (unsigned)d->length, (unsigned)FV_SECRET_DIRECTION_MAX,
                     (unsigned)DIRECTION_MIN_LENGTH);
            /* Printable fallback for the terminal; the display draws arrow bitmaps. */
            static const char symbols[] = "URDL";
            for (uint8_t i = 0u; i < d->length; ++i) {
                view->lines[1u + i / 8u][i % 8u] = symbols[d->values[i]];
            }
            break;
        }
        case FV_ENTRY_METHOD_KEYPAD: {
            const fv_keypad_entry_t *k = &entry->state.keypad;
            const uint8_t value = keypad_value(k->selected);
            snprintf(view->back_action, sizeof(view->back_action),
                     k->length > 0u ? "Delete" : "Back");
            snprintf(view->select_action, sizeof(view->select_action),
                     value == 10u ? "Delete" : value == 11u ? "Done" : "Add");
            view->select_enabled = value == 10u ? k->length > 0u :
                value == 11u ? k->length >= KEYPAD_MIN_LENGTH :
                k->length < FV_SECRET_KEYPAD_MAX;
            keypad_row(&entry->state.keypad, 0u, view->lines[0]);
            keypad_row(&entry->state.keypad, 1u, view->lines[1]);
            keypad_row(&entry->state.keypad, 2u, view->lines[2]);
            break;
        }
        case FV_ENTRY_METHOD_WORD_LIST: {
            const fv_word_entry_t *w = &entry->state.word_list;
            static const char icons[4] = {'\004', '\001', '\002', '\003'};
            char (*choices)[12] = view->word_choices;
            view->word_picker = true;
            view->select_enabled = w->reviewing && words_complete(w);
            snprintf(view->back_action, sizeof(view->back_action), "%s",
                     w->depth > 0u ? "Undo" : w->reviewing ? "Delete" :
                     words_complete(w) ? "Cancel" : w->selected > 0u ? "Delete" : "Back");
            if (w->reviewing) {
                snprintf(view->lines[0], FV_UI_TEXT_CAPACITY, "Review: arrows edit");
            } else {
                snprintf(view->lines[0], FV_UI_TEXT_CAPACITY, "Word %u/4  Pick %u/3",
                         (unsigned)w->selected + 1u, (unsigned)w->depth + 1u);
            }
            for (unsigned choice = 0u; choice < 4u; ++choice) {
                if (w->reviewing) {
                    snprintf(choices[choice], sizeof(choices[choice]), "%c %u %s",
                             icons[choice], choice + 1u, words[w->words[choice]]);
                } else {
                    unsigned size = 16u >> (2u * w->depth);
                    unsigned start = ((unsigned)w->prefix * 4u + choice) * size;
                    if (size == 1u) {
                        snprintf(choices[choice], sizeof(choices[choice]), "%c %s",
                                 icons[choice], words[start]);
                    } else {
                        /* Exact, distinct prefixes avoid overlapping letter ranges. */
                        snprintf(choices[choice], sizeof(choices[choice]), "%c %.3s-%.3s",
                                 icons[choice], words[start], words[start + size - 1u]);
                    }
                }
            }
            for (unsigned row = 0u; row < 2u; ++row) {
                snprintf(view->lines[row + 1u], FV_UI_TEXT_CAPACITY, "%-13s%s",
                         choices[row == 0u ? 0u : 3u],
                         choices[row == 0u ? 1u : 2u]);
            }
            break;
        }
        case FV_ENTRY_METHOD_COUNT: break;
    }
}

bool fv_secret_input_encode(const fv_app_t *app, fv_secret_encoding_t *encoding) {
    return app != NULL && fv_secret_entry_encode(&app->secret_entry, encoding);
}

bool fv_setup_secret_encode(const fv_app_t *app, fv_secret_encoding_t *encoding) {
    return app != NULL && fv_secret_entry_encode(&app->setup_secret_entry, encoding);
}
