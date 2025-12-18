#include <pebble.h>

#define MAX_MC_COUNT 5
#define TIMEOUT_SECONDS 40

static Window *mc_menu_window;
static Window *mc_loading_window;
static Window *mc_restaurant_window;
static Window *mc_more_details_window;

static MenuLayer *mc_main_menu_layer;
static MenuLayer *mc_restaurant_menu_layer;

static TextLayer *mc_loading_text_layer;
static TextLayer *mc_street_text_layer;
static TextLayer *mc_city_text_layer;
static TextLayer *mc_last_checked_text_layer;
static TextLayer *mc_working_text_layer;

AppTimer *mc_timeout_handle = NULL;
AppTimer *loading_dots = NULL;

static BitmapLayer *mc_timeout_bitmap_layer;
static GBitmap *mc_timeout_bitmap;

static GBitmap *working_bitmap;
static GBitmap *broken_bitmap;
static GBitmap *inac_bitmap;

typedef struct {
    char STREET[85];
    char LAST_CHECKED[40];
    char CITY[20];
    char DOT[10];
    bool is_populated;
} mc_struct;

static uint16_t id;
static uint8_t mc_count;
static uint8_t mc_rest_selected;
static uint8_t mc_menu_selected;
static bool is_loading;
static bool is_ready;

static char mc_loaded_buffer[20];
static char full_load_text[12];
static char dots[4];

static mc_struct mc_structs[MAX_MC_COUNT];

static bool fully_populated() {
    for (int i = 0; i < mc_count; i++) {
        if (!mc_structs[i].is_populated) {
            return false;
        }
    }
    return true;
}

/* -- Inbox/Outbox code --- */

static void inbox_received_handler(DictionaryIterator *iterator, void *context) {
    Tuple *mc_message_t = dict_find(iterator, MESSAGE_KEY_mc_message);
   
    if (!mc_message_t) {
        return;
    }
    
    Tuple *id_t = dict_find(iterator, MESSAGE_KEY_id);

    if (mc_timeout_handle != NULL && is_loading && id_t->value->int16 == id) {
        app_timer_cancel(mc_timeout_handle);
        mc_timeout_handle = NULL;
    }

    if (loading_dots != NULL && is_loading && id_t->value->int16 == id) {
        app_timer_cancel(loading_dots);
        loading_dots = NULL;
    }

    if (strcmp(mc_message_t->value->cstring, "mc_ready") == 0) {
        is_ready = true;
    } else if (strcmp(mc_message_t->value->cstring, "mc_data") == 0 && is_loading && id_t->value->int16 == id) {
        Tuple *street_t = dict_find(iterator, MESSAGE_KEY_street);
        Tuple *last_checked_t = dict_find(iterator, MESSAGE_KEY_last_checked);
        Tuple *city_t = dict_find(iterator, MESSAGE_KEY_city);
        Tuple *dot_t = dict_find(iterator, MESSAGE_KEY_dot);

        Tuple *index_t = dict_find(iterator, MESSAGE_KEY_index);
        Tuple *count_t = dict_find(iterator, MESSAGE_KEY_count);

        if (count_t->value->int8 <= MAX_MC_COUNT) {
            mc_count = count_t->value->int8;
        } else {
            mc_count = MAX_MC_COUNT;
        }
        
        if (index_t->value->int8 < MAX_MC_COUNT) {
            if (street_t) {
                strncpy(mc_structs[index_t->value->int8].STREET, 
                                        street_t->value->cstring, 
                                        sizeof(mc_structs[index_t->value->int8].STREET
                                        ));
            }
            if (last_checked_t) {
                strncpy(mc_structs[index_t->value->int8].LAST_CHECKED, 
                                        last_checked_t->value->cstring, 
                                        sizeof(mc_structs[index_t->value->int8].LAST_CHECKED
                                        ));
            }
            if (city_t) {
                strncpy(mc_structs[index_t->value->int8].CITY, 
                                        city_t->value->cstring, 
                                        sizeof(mc_structs[index_t->value->int8].CITY
                                        ));
            }
            if (dot_t) {
                strncpy(mc_structs[index_t->value->int8].DOT, 
                                        dot_t->value->cstring, 
                                        sizeof(mc_structs[index_t->value->int8].DOT
                                        ));
            }

            mc_structs[index_t->value->int8].is_populated = true;
 
            snprintf(mc_loaded_buffer, sizeof(mc_loaded_buffer), 
                "Received %d of %d", index_t->value->int8 + 1, mc_count);
            text_layer_set_text(mc_loading_text_layer, mc_loaded_buffer);

            if (index_t->value->int8 == mc_count - 1) {
                if (fully_populated()) {
                    window_stack_remove(mc_loading_window, false);
                    window_stack_push(mc_restaurant_window, true);
                }
            }
        }
    } else if (strcmp(mc_message_t->value->cstring, "mc_error") == 0 && is_loading && id_t->value->int16 == id) {
        Tuple *error_t = dict_find(iterator, MESSAGE_KEY_error);
        text_layer_set_text(mc_loading_text_layer, error_t->value->cstring);
        is_loading = false;
    }
}

static void outbox_fail_callback(DictionaryIterator *iterator, AppMessageResult reason, void *context) {
    if (window_stack_contains_window(mc_loading_window)) {
        if (mc_timeout_handle != NULL) {
            app_timer_cancel(mc_timeout_handle);
            mc_timeout_handle = NULL;
        }
        if (loading_dots != NULL) {
            app_timer_cancel(loading_dots);
            loading_dots = NULL;
        }
        text_layer_set_text(mc_loading_text_layer, "Failed to send request.");
        is_loading = false;
    }
}

/* -- menu callback code --- */

static int16_t get_header_height(MenuLayer *menu_layer, uint16_t section_index, void *data) {
    return 16;
}

static int16_t get_cell_height(MenuLayer *menu_layer, MenuIndex *cell_index, void *callback_context) {
    #if PBL_DISPLAY_HEIGHT == 168
    return 44;
    #elif PBL_DISPLAY_HEIGHT == 228
    return 54;
    #endif
}

static void draw_mc_restaurant_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *callback_context) {
    switch (mc_menu_selected) {
        case 0:
        menu_cell_basic_header_draw(ctx, cell_layer, "Nearby locations");
            break;
        case 1:
        menu_cell_basic_header_draw(ctx, cell_layer, "Saved locations");
            break;
    }
}

static uint16_t get_mc_row_callback(struct MenuLayer *s_menu_layer, uint16_t section_index, void *callback_context) {
    return mc_count;
}

static void draw_mc_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index,
                                     void *callback_context) 
{
    char *mc_street = mc_structs[cell_index->row].STREET;
    char *mc_dot = mc_structs[cell_index->row].DOT;

    #if PBL_DISPLAY_HEIGHT == 168
    GRect bitmap_bounds = GRect(5, 25, 15, 15);
    #elif PBL_DISPLAY_HEIGHT == 228
    GRect bitmap_bounds = GRect(9, 28, 21, 21);
    #endif

    #if PBL_COLOR
    graphics_context_set_compositing_mode(ctx, GCompOpSet);
    #else
    graphics_context_set_compositing_mode(ctx, 
        menu_cell_layer_is_highlighted(cell_layer) ? GCompOpAssignInverted : GCompOpAssign );
    #endif
    
    if (strcmp(mc_dot, "working") == 0) {
        graphics_draw_bitmap_in_rect(ctx, working_bitmap, bitmap_bounds);
    } else if (strcmp(mc_dot, "broken") == 0) {
        graphics_draw_bitmap_in_rect(ctx, broken_bitmap, bitmap_bounds);
    } else {
        graphics_draw_bitmap_in_rect(ctx, inac_bitmap, bitmap_bounds);
    }

    char final_mc_dot[18];
    snprintf(final_mc_dot, sizeof(final_mc_dot), "      %s", mc_dot);

    menu_cell_basic_draw(ctx, cell_layer, mc_street, final_mc_dot, NULL);
}

static void mc_restaurant_selection_callback(struct MenuLayer *s_menu_layer, MenuIndex *cell_index, void *callback_context) {
    mc_rest_selected = cell_index->row;
    window_stack_push(mc_more_details_window, true);
}

static void draw_mc_menu_header(GContext *ctx, const Layer *cell_layer, uint16_t section_index, void *callback_context) {
    menu_cell_basic_header_draw(ctx, cell_layer, "mcbroken");
}

static void reset_mcdata() {
    for (int i = 0; i < MAX_MC_COUNT; i++) {
        memset(&mc_structs->STREET[i], 0, sizeof(&mc_structs->STREET[i]));
        memset(&mc_structs->LAST_CHECKED[i], 0, sizeof(&mc_structs->LAST_CHECKED[i]));
        memset(&mc_structs->CITY[i], 0, sizeof(&mc_structs->CITY[i])); 
        memset(&mc_structs->DOT[i], 0, sizeof(&mc_structs->DOT[i]));
        mc_structs[i].is_populated = false;
    }

    mc_count = 0;
}

static void load_mcdata() {
    if (!is_loading) {
        reset_mcdata();
        is_loading = true;
        
        DictionaryIterator *iter;
        app_message_outbox_begin(&iter);

        dict_write_int(iter, MESSAGE_KEY_id, &id, sizeof(int16_t), true);

        switch (mc_menu_selected) {
            case 0:
            dict_write_cstring(iter, MESSAGE_KEY_mc_message, "load_mcdata_by_loc");
                break;
            case 1:
            dict_write_cstring(iter, MESSAGE_KEY_mc_message, "load_mcdata_by_saved");
                break;
        }
        
        app_message_outbox_send();
    }
}

static void ready_callback() {
    if (window_stack_contains_window(mc_loading_window)) {
        if (is_ready) {
            load_mcdata();
        } else {
            app_timer_register(1000, ready_callback, NULL);
        }
    }
}

static void mc_main_menu_selection_callback(struct MenuLayer *s_menu_layer, MenuIndex *cell_index, void *callback_context) {
    mc_menu_selected = cell_index->row;
   
    if (connection_service_peek_pebble_app_connection()) {
        srand(time(NULL));
        id = (rand() % 16967) + 67; // Funny six seven number. Laugh.

        if (is_ready) {
            load_mcdata();
        } else {
            /* wait one second, then check if it's ready again */
            app_timer_register(1000, ready_callback, NULL);
        }
    }

    window_stack_push(mc_loading_window, true);
}

static uint16_t get_mc_menu_row_callback(struct MenuLayer *s_menu_layer, uint16_t section_index, void *callback_context) {
    return 2;
}

static void draw_mc_menu_row_callback(GContext *ctx, const Layer *cell_layer, MenuIndex *cell_index, void *callback_context) {
    switch (cell_index->row) {
        case 0:
        menu_cell_basic_draw(ctx, cell_layer, "Nearby", NULL, NULL);
            break;
        case 1:
        menu_cell_basic_draw(ctx, cell_layer, "Saved", NULL, NULL);
            break;
    }
}

/* --- window code --- */

static void mc_more_details_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    /* This code sucks. I have a skill issue  */

    #if PBL_DISPLAY_HEIGHT == 168
    mc_street_text_layer = text_layer_create(GRect(0, 0, bounds.size.w, 60));
    text_layer_set_font(mc_street_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    #elif PBL_DISPLAY_HEIGHT == 228
    mc_street_text_layer = text_layer_create(GRect(0, 0, bounds.size.w, 90));
    text_layer_set_font(mc_street_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    #endif

    text_layer_set_text_color(mc_street_text_layer, GColorBlack);
    text_layer_set_background_color(mc_street_text_layer, GColorClear);
    text_layer_set_text(mc_street_text_layer, mc_structs[mc_rest_selected].STREET);
    text_layer_set_text_alignment(mc_street_text_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(mc_street_text_layer, GTextOverflowModeFill);

    uint8_t offset = 0;
    uint8_t content_size = text_layer_get_content_size(mc_street_text_layer).h;

    #if PBL_DISPLAY_HEIGHT == 168
    switch (content_size) {
        case 18:
            offset = content_size + 24;
            break;
        case 36:
            offset = content_size + 10;
            break;
        case 54:
            offset = content_size + 2;
            break;
    }
    #elif PBL_DISPLAY_HEIGHT == 228
    switch (content_size) {
        case 24:
            offset = content_size + 40;
            break;
        case 48:
            offset = content_size + 22;
            break;
        case 72:
            offset = content_size + 8;
            break;
    }
    #endif

    mc_city_text_layer = text_layer_create(GRect(0, offset, bounds.size.w, 30));

    #if PBL_DISPLAY_HEIGHT == 168
    text_layer_set_font(mc_city_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
    #elif PBL_DISPLAY_HEIGHT == 228
    text_layer_set_font(mc_city_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28));
    #endif

    text_layer_set_text_color(mc_city_text_layer, GColorBlack);
    text_layer_set_background_color(mc_city_text_layer, GColorClear);
    text_layer_set_text(mc_city_text_layer, mc_structs[mc_rest_selected].CITY);
    text_layer_set_text_alignment(mc_city_text_layer, GTextAlignmentCenter);
    
    #if PBL_DISPLAY_HEIGHT == 168
    mc_last_checked_text_layer = text_layer_create(GRect(0, 
        content_size == 54 ? offset + 40 : offset + 46, bounds.size.w, 40));
    text_layer_set_font(mc_last_checked_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18));
    #elif PBL_DISPLAY_HEIGHT == 228
    mc_last_checked_text_layer = text_layer_create(GRect(0, 
        content_size == 72 ? offset + 52 : offset + 58, bounds.size.w, 70));
    text_layer_set_font(mc_last_checked_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24));
    #endif

    text_layer_set_overflow_mode(mc_last_checked_text_layer, GTextOverflowModeWordWrap);
    text_layer_set_text_color(mc_last_checked_text_layer, GColorBlack);
    text_layer_set_background_color(mc_last_checked_text_layer, GColorClear);
    text_layer_set_text(mc_last_checked_text_layer, mc_structs[mc_rest_selected].LAST_CHECKED);
    text_layer_set_text_alignment(mc_last_checked_text_layer, GTextAlignmentCenter);
  
    if (strcmp(mc_structs[mc_rest_selected].DOT, "working") == 0 
     || strcmp(mc_structs[mc_rest_selected].DOT, "broken") == 0) {
        layer_set_hidden(text_layer_get_layer(mc_last_checked_text_layer), false);
        #if PBL_DISPLAY_HEIGHT == 168
        mc_working_text_layer = text_layer_create(GRect(0, bounds.size.h -34, bounds.size.w, 40));
        text_layer_set_font(mc_working_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
        #elif PBL_DISPLAY_HEIGHT == 228
        mc_working_text_layer = text_layer_create(GRect(0, bounds.size.h -40, bounds.size.w, 40));
        text_layer_set_font(mc_working_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_28_BOLD));
        #endif
    } else {
        layer_set_hidden(text_layer_get_layer(mc_last_checked_text_layer), true);
        #if PBL_DISPLAY_HEIGHT == 168
        mc_working_text_layer = text_layer_create(GRect(0, bounds.size.h -44, bounds.size.w, 40));
        text_layer_set_font(mc_working_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
        #elif PBL_DISPLAY_HEIGHT == 228
        mc_working_text_layer = text_layer_create(GRect(0, bounds.size.h -60, bounds.size.w, 70));
        text_layer_set_font(mc_working_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
        #endif
    }

    text_layer_set_background_color(mc_working_text_layer, GColorClear);
    text_layer_set_text_color(mc_working_text_layer, GColorBlack);
    text_layer_set_text_alignment(mc_working_text_layer, GTextAlignmentCenter);
    text_layer_set_overflow_mode(mc_working_text_layer, GTextOverflowModeWordWrap);

    if (strcmp(mc_structs[mc_rest_selected].DOT, "working") == 0) {
        text_layer_set_text(mc_working_text_layer, "Machine Working");
    } else if (strcmp(mc_structs[mc_rest_selected].DOT, "broken") == 0) {
        text_layer_set_text(mc_working_text_layer, "Machine Broken");
    } else {
        text_layer_set_text(mc_working_text_layer, "Status could not be determined");
    }
    
    layer_add_child(window_layer, text_layer_get_layer(mc_street_text_layer));
    layer_add_child(window_layer, text_layer_get_layer(mc_city_text_layer));
    layer_add_child(window_layer, text_layer_get_layer(mc_last_checked_text_layer));
    layer_add_child(window_layer, text_layer_get_layer(mc_working_text_layer));
}

static void mc_more_details_unload(Window *window) {
    text_layer_destroy(mc_street_text_layer);
    text_layer_destroy(mc_city_text_layer);
    text_layer_destroy(mc_last_checked_text_layer);
    text_layer_destroy(mc_working_text_layer);
    mc_rest_selected = 0;
}

static void mc_restaurant_window_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    mc_restaurant_menu_layer = menu_layer_create(bounds);
    
    #if PBL_COLOR
        menu_layer_set_highlight_colors(mc_restaurant_menu_layer, GColorChromeYellow, GColorBlack);
        #if PBL_DISPLAY_HEIGHT == 168
        working_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WORKING);
        broken_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BROKEN);
        inac_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_INACTIVE);
        #elif PBL_DISPLAY_HEIGHT == 228
        working_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WORKING_HIRES);
        broken_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BROKEN_HIRES);
        inac_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_INACTIVE_HIRES);
        #endif
    #else
        working_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_WORKING_BW);
        broken_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_BROKEN_BW);
        inac_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_INACTIVE_BW);
    #endif

    static const MenuLayerCallbacks mc_menu_callbacks = {
        .get_num_rows = get_mc_row_callback,
        .get_cell_height = get_cell_height,
        .draw_row = draw_mc_row_callback,
        .select_click = mc_restaurant_selection_callback,
        .draw_header = draw_mc_restaurant_header,
        .get_header_height = get_header_height
    };

    menu_layer_set_callbacks(mc_restaurant_menu_layer, NULL, mc_menu_callbacks);
    menu_layer_set_click_config_onto_window(mc_restaurant_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(mc_restaurant_menu_layer));
}


static void mc_restaurant_window_unload(Window *window) {
    menu_layer_destroy(mc_restaurant_menu_layer);
}

static void shut_up() {
    /* plug our ears and tell the phone to shut up (stop sending app messages, hopefully) */
    is_loading = false;
    DictionaryIterator *iter;
    app_message_outbox_begin(&iter);
    dict_write_cstring(iter, MESSAGE_KEY_mc_message, "shut_up");
    app_message_outbox_send();
}

static void mc_timeout_callback(void *data) {
    Layer *window_layer = (Layer*)data;
    if (is_loading) {
        layer_add_child(window_layer, bitmap_layer_get_layer(mc_timeout_bitmap_layer));
        shut_up();
        mc_timeout_handle = NULL;
    }
}

static void loading_text_callback() {
    if (strlen(dots) < 3) {
        strncat(dots, ".", 1);
    } else {
        memset(&dots, 0, sizeof(dots));
    }
        
    snprintf(full_load_text, sizeof(full_load_text), "Waiting%s", dots);
    text_layer_set_text(mc_loading_text_layer, full_load_text);
    loading_dots = app_timer_register(500, loading_text_callback, NULL);
}

static void mc_loading_screen_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    #if PBL_COLOR
        #if PBL_DISPLAY_HEIGHT == 168
        mc_timeout_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MCHADIT);
        #elif PBL_DISPLAY_HEIGHT == 228
        mc_timeout_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MCHADIT_HIRES);
        #endif
    #else
    mc_timeout_bitmap = gbitmap_create_with_resource(RESOURCE_ID_IMAGE_MCHADIT_BW);
    #endif

    mc_timeout_bitmap_layer = bitmap_layer_create(bounds);
    bitmap_layer_set_bitmap(mc_timeout_bitmap_layer, mc_timeout_bitmap);

    #if PBL_DISPLAY_HEIGHT == 168
    mc_loading_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 -10, bounds.size.w, 60)); 
    #elif PBL_DISPLAY_HEIGHT == 228
    mc_loading_text_layer = text_layer_create(GRect(0, bounds.size.h / 2 -15, bounds.size.w, 90));
    #endif
    text_layer_set_text_color(mc_loading_text_layer, GColorBlack);

    void *callback_data = window_layer;

    if (connection_service_peek_pebble_app_connection()) {
        text_layer_set_text(mc_loading_text_layer, "Waiting");
        mc_timeout_handle = app_timer_register(TIMEOUT_SECONDS * 1000, mc_timeout_callback, callback_data);
        loading_dots = app_timer_register(500, loading_text_callback, NULL);
    } else {
        is_loading = false;
        text_layer_set_text(mc_loading_text_layer, "Phone not connected.");
    }
    
    #if PBL_DISPLAY_HEIGHT == 168
    text_layer_set_font(mc_loading_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_18_BOLD));
    #elif PBL_DISPLAY_HEIGHT == 228
    text_layer_set_font(mc_loading_text_layer, fonts_get_system_font(FONT_KEY_GOTHIC_24_BOLD));
    #endif
    text_layer_set_text_alignment(mc_loading_text_layer, GTextAlignmentCenter);

    layer_add_child(window_layer, text_layer_get_layer(mc_loading_text_layer));
}

static void mc_loading_screen_unload(Window *window) {
    if (mc_timeout_handle != NULL) {
        app_timer_cancel(mc_timeout_handle);
        mc_timeout_handle = NULL;
    }

    if (loading_dots != NULL) {
        app_timer_cancel(loading_dots);
        loading_dots = NULL;
    }

    shut_up();

    memset(mc_loaded_buffer, 0, sizeof(mc_loaded_buffer));
    memset(&dots, 0, sizeof(dots));

    text_layer_destroy(mc_loading_text_layer);
    bitmap_layer_destroy(mc_timeout_bitmap_layer);
    gbitmap_destroy(mc_timeout_bitmap);
}

static void mc_main_menu_load(Window *window) {
    Layer *window_layer = window_get_root_layer(window);
    GRect bounds = layer_get_bounds(window_layer);

    mc_main_menu_layer = menu_layer_create(bounds);

    #if PBL_COLOR
    menu_layer_set_highlight_colors(mc_main_menu_layer, GColorChromeYellow, GColorBlack);
    #endif

    static const MenuLayerCallbacks mc_menu_callbacks = {
        .get_num_rows = get_mc_menu_row_callback,
        .get_cell_height = get_cell_height,
        .draw_row = draw_mc_menu_row_callback,
        .select_click = mc_main_menu_selection_callback,
        .draw_header = draw_mc_menu_header,
        .get_header_height = get_header_height
    };

    menu_layer_set_callbacks(mc_main_menu_layer, NULL, mc_menu_callbacks);
    menu_layer_set_click_config_onto_window(mc_main_menu_layer, window);
    layer_add_child(window_layer, menu_layer_get_layer(mc_main_menu_layer));
}

static void mc_main_menu_unload(Window *window) {
    menu_layer_destroy(mc_main_menu_layer);
}

static void init() {
    mc_menu_window = window_create();
    window_set_window_handlers(mc_menu_window, 
    (WindowHandlers) {
        .load = mc_main_menu_load,
        .unload = mc_main_menu_unload
    });

    mc_loading_window = window_create();
    window_set_window_handlers(mc_loading_window, 
    (WindowHandlers) {
        .load = mc_loading_screen_load,
        .unload = mc_loading_screen_unload
    });
    
    mc_restaurant_window = window_create();
    window_set_window_handlers(mc_restaurant_window, 
    (WindowHandlers) {
        .load = mc_restaurant_window_load,
        .unload = mc_restaurant_window_unload
    });

    mc_more_details_window = window_create();
    window_set_window_handlers(mc_more_details_window, 
    (WindowHandlers) {
        .load = mc_more_details_load,
        .unload = mc_more_details_unload
    });

    window_stack_push(mc_menu_window, true);
    is_ready = false;
    reset_mcdata();

    app_message_register_inbox_received(inbox_received_handler);
    app_message_register_outbox_failed(outbox_fail_callback);

    const int inbox_size = 512;
    const int outbox_size = 128;
    app_message_open(inbox_size, outbox_size);
}

static void deinit() {
    app_message_deregister_callbacks();
    window_destroy(mc_menu_window);
    window_destroy(mc_loading_window);
    window_destroy(mc_restaurant_window);
    window_destroy(mc_more_details_window);
}

int main(void) {
    init();
    app_event_loop();
    deinit();
}
