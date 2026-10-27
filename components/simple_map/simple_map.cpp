#include "simple_map.hpp"
#include <math.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

// Static member definitions
lv_obj_t* SimpleMap::map_container = nullptr;
lv_obj_t* SimpleMap::map_group = nullptr;
lv_obj_t** SimpleMap::tile_widgets = nullptr;
int SimpleMap::grid_cols = 0;
int SimpleMap::grid_rows = 0;
int SimpleMap::tile_count = 0;
lv_obj_t* SimpleMap::input_panel = nullptr;
lv_obj_t* SimpleMap::lat_textarea = nullptr;
lv_obj_t* SimpleMap::lon_textarea = nullptr;
lv_obj_t* SimpleMap::zoom_slider = nullptr;
lv_obj_t* SimpleMap::zoom_label = nullptr;
lv_obj_t* SimpleMap::update_button = nullptr;
lv_obj_t* SimpleMap::keyboard = nullptr;
lv_obj_t* SimpleMap::loading_popup = nullptr;
lv_obj_t* SimpleMap::gps_marker = nullptr;
lv_obj_t* SimpleMap::gps_heading_line = nullptr;
lv_point_precise_t SimpleMap::gps_heading_points[2] = {{11, 11}, {11, 3}};
lv_obj_t* SimpleMap::gps_status_label = nullptr;
lv_obj_t* SimpleMap::gps_follow_button = nullptr;
double SimpleMap::current_lat = 0.0;
double SimpleMap::current_lon = 0.0;
double SimpleMap::gps_lat = 0.0;
double SimpleMap::gps_lon = 0.0;
int SimpleMap::current_zoom = 15;
int SimpleMap::min_zoom = 10;
int SimpleMap::max_zoom = 19;
bool SimpleMap::initialized = false;
bool SimpleMap::is_loading = false;
bool SimpleMap::gps_fix_received = false;
bool SimpleMap::gps_fix_valid = false;
bool SimpleMap::gps_follow_enabled = true;
bool SimpleMap::programmatic_scroll = false;
bool SimpleMap::pending_follow_center = false;
bool SimpleMap::pending_drag_center = false;
bool SimpleMap::drag_active = false;
uint8_t SimpleMap::gps_satellites = 0;
float SimpleMap::gps_heading_degrees = 0.0f;
bool SimpleMap::gps_heading_valid = false;
uint32_t SimpleMap::last_scroll_time = 0;
uint32_t SimpleMap::last_gps_update_time = 0;
map_tiles_handle_t SimpleMap::map_handle = nullptr;

bool SimpleMap::set_zoom_range(int new_min_zoom, int new_max_zoom) {
    if (new_min_zoom < 0 || new_max_zoom > 30 || new_min_zoom > new_max_zoom) {
        printf("SimpleMap: Invalid zoom range %d..%d\n", new_min_zoom, new_max_zoom);
        return false;
    }

    min_zoom = new_min_zoom;
    max_zoom = new_max_zoom;
    if (current_zoom < min_zoom) {
        current_zoom = min_zoom;
    } else if (current_zoom > max_zoom) {
        current_zoom = max_zoom;
    }

    if (zoom_slider) {
        lv_slider_set_range(zoom_slider, min_zoom, max_zoom);
        lv_slider_set_value(zoom_slider, current_zoom, LV_ANIM_OFF);
    }
    return true;
}

bool SimpleMap::init(lv_obj_t* parent_screen) {
    if (initialized) return true;
    
    // Initialize the map tiles component
    map_tiles_config_t config = {
        .base_path = "/sdcard",
        .tile_folders = {"tiles1"}, 
        .tile_type_count = 1,
        .grid_cols = MAP_TILES_DEFAULT_GRID_COLS,
        .grid_rows = MAP_TILES_DEFAULT_GRID_ROWS,
        .default_zoom = 18,
        .use_spiram = true,
        .default_tile_type = 0  // Start with tiles1
    };
    
    map_handle = map_tiles_init(&config);
    if (!map_handle) {
        printf("SimpleMap: Failed to initialize map tiles component\n");
        return false;
    }
    
    // Cache grid dimensions for performance
    map_tiles_get_grid_size(map_handle, &grid_cols, &grid_rows);
    tile_count = map_tiles_get_tile_count(map_handle);
    
    printf("SimpleMap: Initialized with grid size %dx%d (%d tiles)\n", grid_cols, grid_rows, tile_count);

    lv_display_t *disp = lv_display_get_default();    
    lv_coord_t _width = lv_display_get_horizontal_resolution(disp);
    lv_coord_t _height = lv_display_get_vertical_resolution(disp);
    
    // Keep LVGL's programmatic scroll storage, but handle pointer dragging ourselves.
    // LVGL 9 locks a pointer scroll gesture to one axis, which prevents free 2D panning.
    map_container = lv_obj_create(parent_screen);
    lv_obj_set_size(map_container, _width, _height);  // Match device resolution
    lv_obj_center(map_container);
    lv_obj_clear_flag(map_container, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(map_container, LV_DIR_ALL);
    lv_obj_clear_flag(map_container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_border_width(map_container, 0, 0);
    lv_obj_set_style_radius(map_container, 0, 0);
    lv_obj_add_event_cb(map_container, map_scroll_event_cb, LV_EVENT_SCROLL_END, NULL);
    lv_obj_add_event_cb(map_container, map_scroll_event_cb, LV_EVENT_SCROLL, NULL);
    lv_obj_add_event_cb(map_container, map_scroll_event_cb, LV_EVENT_SCROLL_BEGIN, NULL);
    lv_obj_set_style_bg_color(map_container, lv_color_black(), 0);
    lv_obj_set_scrollbar_mode(map_container, LV_SCROLLBAR_MODE_OFF);
    
    // Create tile widgets
    create_tile_widgets();
    
    // Create input panel
    create_input_panel(parent_screen);
    create_gps_controls(parent_screen);
    
    initialized = true;
    return true;
}

void SimpleMap::create_gps_controls(lv_obj_t* parent_screen) {
    gps_marker = lv_obj_create(map_group);
    lv_obj_remove_style_all(gps_marker);
    lv_obj_clear_flag(gps_marker, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(gps_marker, 22, 22);
    lv_obj_set_style_radius(gps_marker, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(gps_marker, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(gps_marker, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(gps_marker, 3, 0);
    lv_obj_set_style_border_color(gps_marker, lv_color_white(), 0);
    gps_heading_line = lv_line_create(gps_marker);
    lv_line_set_points_mutable(gps_heading_line, gps_heading_points, 2);
    lv_obj_set_style_line_width(gps_heading_line, 3, 0);
    lv_obj_set_style_line_color(gps_heading_line, lv_color_white(), 0);
    lv_obj_set_style_line_rounded(gps_heading_line, true, 0);
    lv_obj_add_flag(gps_heading_line, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(gps_marker, LV_OBJ_FLAG_HIDDEN);

    gps_status_label = lv_label_create(parent_screen);
    lv_label_set_text(gps_status_label, LV_SYMBOL_GPS " No fix");
    lv_obj_set_style_text_color(gps_status_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(gps_status_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(gps_status_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(gps_status_label, 8, 0);
    lv_obj_set_style_pad_ver(gps_status_label, 5, 0);
    lv_obj_set_style_radius(gps_status_label, 4, 0);
    lv_obj_align(gps_status_label, LV_ALIGN_TOP_RIGHT, -16, 16);

    gps_follow_button = lv_button_create(parent_screen);
    lv_obj_set_size(gps_follow_button, 44, 44);
    lv_obj_align(gps_follow_button, LV_ALIGN_BOTTOM_RIGHT, -16, -16);
    lv_obj_set_style_radius(gps_follow_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_add_event_cb(gps_follow_button, gps_follow_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t* icon = lv_label_create(gps_follow_button);
    lv_label_set_text(icon, LV_SYMBOL_GPS);
    lv_obj_center(icon);
    update_gps_status();
}

void SimpleMap::create_tile_widgets() {
    // Use cached grid dimensions for better performance
    if (grid_cols <= 0 || grid_rows <= 0 || tile_count <= 0) {
        printf("SimpleMap: Invalid grid dimensions cached\n");
        return;
    }
    
    // Allocate tile_widgets array if not already done
    if (!tile_widgets) {
        tile_widgets = (lv_obj_t**)calloc(tile_count, sizeof(lv_obj_t*));
        if (!tile_widgets) {
            printf("Failed to allocate tile_widgets array\n");
            return;
        }
    }
    
    // Create map group with full tile dimensions (like map_display.cpp)
    if (!map_group) {
        map_group = lv_obj_create(map_container);
        lv_obj_set_size(map_group, MAP_TILES_TILE_SIZE * grid_cols, MAP_TILES_TILE_SIZE * grid_rows);
        lv_obj_set_style_pad_all(map_group, 0, 0);
        lv_obj_set_style_border_width(map_group, 0, 0);
        lv_obj_clear_flag(map_group, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(map_group, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_flag(map_group, LV_OBJ_FLAG_PRESS_LOCK);
        lv_obj_add_event_cb(map_group, map_drag_event_cb, LV_EVENT_PRESSED, NULL);
        lv_obj_add_event_cb(map_group, map_drag_event_cb, LV_EVENT_PRESSING, NULL);
        lv_obj_add_event_cb(map_group, map_drag_event_cb, LV_EVENT_RELEASED, NULL);
        lv_obj_add_event_cb(map_group, map_drag_event_cb, LV_EVENT_PRESS_LOST, NULL);
        lv_obj_set_pos(map_group, 0, 0);
        
        // Create grid of image widgets for tiles
        for (int i = 0; i < tile_count; i++) {
            tile_widgets[i] = lv_image_create(map_group);
            
            int row = i / grid_cols;
            int col = i % grid_cols;
            
            // Position using full TILE_SIZE (like map_display.cpp)
            lv_obj_set_pos(tile_widgets[i], col * MAP_TILES_TILE_SIZE, row * MAP_TILES_TILE_SIZE);
            lv_obj_set_size(tile_widgets[i], MAP_TILES_TILE_SIZE, MAP_TILES_TILE_SIZE);
            
            // Set default background while tiles load
            lv_obj_set_style_bg_color(tile_widgets[i], lv_color_make(200, 200, 200), 0);
            lv_obj_set_style_bg_opa(tile_widgets[i], LV_OPA_COVER, 0);
        }
    }
}

void SimpleMap::show_location(double latitude, double longitude, int zoom_level) {
    if (zoom_level < min_zoom || zoom_level > max_zoom) {
        printf("SimpleMap: Zoom %d outside configured range %d..%d\n", zoom_level, min_zoom, max_zoom);
        return;
    }
    if (!initialized || !map_handle) return;
    
    // Update current position and zoom
    current_lat = latitude;
    current_lon = longitude;
    current_zoom = zoom_level;
    
    // Set zoom level in the component
    map_tiles_set_zoom(map_handle, zoom_level);
    
    // Update input panel widgets
    if (lat_textarea) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", latitude);
        lv_textarea_set_text(lat_textarea, buf);
    }
    if (lon_textarea) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", longitude);
        lv_textarea_set_text(lon_textarea, buf);
    }
    if (zoom_slider) {
        lv_slider_set_value(zoom_slider, zoom_level, LV_ANIM_OFF);
    }
    
    // Update zoom label
    update_zoom_label();
    
    // Set the tile center based on GPS coordinates
    map_tiles_set_center_from_gps(map_handle, latitude, longitude);
    
    load_map_tiles();
}

void SimpleMap::update_location(double latitude, double longitude) {
    show_location(latitude, longitude, current_zoom);
}

void SimpleMap::update_gps_fix(double latitude, double longitude, uint8_t satellites, bool valid) {
    if (!initialized || !map_handle) return;

    gps_satellites = satellites;
    gps_fix_valid = valid;
    if (!valid) {
        update_gps_status();
        return;
    }

    gps_lat = latitude;
    gps_lon = longitude;
    gps_fix_received = true;
    update_gps_marker();
    update_gps_status();

    if (!gps_follow_enabled) return;

    current_lat = gps_lat;
    current_lon = gps_lon;
    if (lat_textarea && !lv_obj_has_state(lat_textarea, LV_STATE_FOCUSED)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", current_lat);
        lv_textarea_set_text(lat_textarea, buf);
    }
    if (lon_textarea && !lv_obj_has_state(lon_textarea, LV_STATE_FOCUSED)) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", current_lon);
        lv_textarea_set_text(lon_textarea, buf);
    }

    if (gps_near_tile_grid_edge() && !is_loading) {
        map_tiles_set_center_from_gps(map_handle, gps_lat, gps_lon);
        pending_follow_center = true;
        update_gps_marker();
        load_map_tiles();
    } else if (!is_loading) {
        center_map_on_gps();
    }
}

void SimpleMap::update_heading(float heading_degrees, bool valid)
{
    gps_heading_degrees = heading_degrees;
    gps_heading_valid = valid;

    if (!gps_heading_line || !gps_fix_received) {
        return;
    }

    if (!valid) {
        if (!lv_obj_has_flag(gps_heading_line, LV_OBJ_FLAG_HIDDEN)) {
            lv_obj_add_flag(gps_heading_line, LV_OBJ_FLAG_HIDDEN);
        }
        return;
    }

    const float radians = heading_degrees * (3.14159265358979323846f / 180.0f);
    const int32_t endpoint_x = 11 + (int32_t)lroundf(sinf(radians) * 8.0f);
    const int32_t endpoint_y = 11 - (int32_t)lroundf(cosf(radians) * 8.0f);
    const bool endpoint_changed = gps_heading_points[1].x != endpoint_x ||
                                  gps_heading_points[1].y != endpoint_y;

    if (endpoint_changed) {
        gps_heading_points[1].x = endpoint_x;
        gps_heading_points[1].y = endpoint_y;
        lv_line_set_points_mutable(gps_heading_line, gps_heading_points, 2);
    }
    if (lv_obj_has_flag(gps_heading_line, LV_OBJ_FLAG_HIDDEN)) {
        lv_obj_clear_flag(gps_heading_line, LV_OBJ_FLAG_HIDDEN);
    }
}

void SimpleMap::set_gps_follow(bool enabled) {
    gps_follow_enabled = enabled;
    update_gps_status();
    if (enabled && gps_fix_valid && gps_fix_received) {
        update_gps_fix(gps_lat, gps_lon, gps_satellites, true);
    }
}

bool SimpleMap::is_gps_following() {
    return gps_follow_enabled;
}

void SimpleMap::set_satellite_view(bool satellite) {
    if (!initialized || !map_handle) return;
    
    // Convert boolean to tile type (0 = regular, 1 = satellite)
    int tile_type = satellite ? 1 : 0;
    
    // Set tile type in the component
    if (map_tiles_set_tile_type(map_handle, tile_type)) {
        // Reload tiles with the new tile type
        load_map_tiles();
    }
}

bool SimpleMap::set_tile_type(int tile_type) {
    if (!initialized || !map_handle) return false;
    
    // Set tile type in the component
    if (map_tiles_set_tile_type(map_handle, tile_type)) {
        // Reload tiles with the new tile type
        load_map_tiles();
        return true;
    }
    return false;
}

int SimpleMap::get_tile_type() {
    if (!initialized || !map_handle) return -1;
    return map_tiles_get_tile_type(map_handle);
}

int SimpleMap::get_tile_type_count() {
    if (!initialized || !map_handle) return 0;
    return map_tiles_get_tile_type_count(map_handle);
}

const char* SimpleMap::get_tile_type_folder(int tile_type) {
    if (!initialized || !map_handle) return nullptr;
    return map_tiles_get_tile_type_folder(map_handle, tile_type);
}

void SimpleMap::load_map_tiles() {
    if (!initialized || !map_handle) return;
    
    // Check if already loading to prevent multiple simultaneous loads
    if (is_loading) {
        printf("SimpleMap: Already loading, skipping...\n");
        return;
    }
    
    is_loading = true;
    printf("SimpleMap: Starting tile loading...\n");
    
    // Use a single-shot timer with longer delay to reduce CPU pressure
    lv_timer_create([](lv_timer_t *t) {
        lv_timer_del(t);  // Delete this timer immediately
        
        uint32_t start_time = esp_timer_get_time() / 1000;
        
        // Get current tile position
        int tx, ty;
        map_tiles_get_position(map_handle, &tx, &ty);
        
        printf("SimpleMap: Loading %dx%d grid at position (%d,%d) at zoom %d\n", 
               grid_cols, grid_rows, tx, ty, map_tiles_get_zoom(map_handle));
        
        // The timer runs in the LVGL task, so redraw cannot occur in the middle
        // of this batch. Keep the existing map visible while reading tiles.
        for (int row = 0; row < grid_rows; row++) {
            for (int col = 0; col < grid_cols; col++) {
                int index = row * grid_cols + col;
                int tile_x = tx + col;
                int tile_y = ty + row;
                
                // Load tile from SD card
                bool loaded = map_tiles_load_tile(map_handle, index, tile_x, tile_y);
                
                if (loaded) {
                    // Get the tile image data and set it
                    lv_image_dsc_t* tile_img = map_tiles_get_image(map_handle, index);
                    if (tile_img && tile_img->data) {
                        lv_image_set_src(tile_widgets[index], (const void *)tile_img);
                        // Only clear background if we have valid image
                        lv_obj_set_style_bg_opa(tile_widgets[index], LV_OPA_TRANSP, 0);
                    }
                }
            }
        }
        
        lv_obj_invalidate(map_group);

        update_gps_marker();
        if (pending_follow_center || pending_drag_center) {
            pending_follow_center = false;
            pending_drag_center = false;
            center_map_on_gps();
        }
        
        uint32_t end_time = esp_timer_get_time() / 1000;
        printf("SimpleMap: Tile loading completed in %lu ms\n", end_time - start_time);
        
        // Clear loading flag
        is_loading = false;
        
    }, 50, NULL);  // Increased delay to reduce CPU pressure
}

void SimpleMap::map_drag_event_cb(lv_event_t *e)
{
    if (!initialized || !map_container) return;

    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_PRESSED) {
        if (drag_active) return;
        drag_active = true;
        lv_obj_send_event(map_container, LV_EVENT_SCROLL_BEGIN, NULL);
        return;
    }

    if (code == LV_EVENT_PRESSING) {
        if (!drag_active) return;

        lv_indev_t *indev = lv_indev_active();
        if (!indev) return;

        lv_point_t vector = {0, 0};
        lv_indev_get_vect(indev, &vector);
        int32_t dx = vector.x;
        int32_t dy = vector.y;
        if (dx == 0 && dy == 0) return;

        // Retain a small elastic area so releasing beyond an edge can shift the
        // tile grid, while preventing an unbounded drag away from the map.
        constexpr int32_t kMaxOverscroll = 128;
        int32_t left = lv_obj_get_scroll_left(map_container);
        int32_t right = lv_obj_get_scroll_right(map_container);
        int32_t top = lv_obj_get_scroll_top(map_container);
        int32_t bottom = lv_obj_get_scroll_bottom(map_container);

        if (dx > left + kMaxOverscroll) dx = left + kMaxOverscroll;
        if (dx < -(right + kMaxOverscroll)) dx = -(right + kMaxOverscroll);
        if (dy > top + kMaxOverscroll) dy = top + kMaxOverscroll;
        if (dy < -(bottom + kMaxOverscroll)) dy = -(bottom + kMaxOverscroll);

        // The public API emits begin/end events around each sample. While a
        // drag is active those synthetic end events are ignored below; the
        // release event performs the one final tile-boundary check.
        lv_obj_scroll_by(map_container, dx, dy, LV_ANIM_OFF);
        return;
    }

    if ((code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) && drag_active) {
        drag_active = false;
        lv_obj_send_event(map_container, LV_EVENT_SCROLL_END, NULL);
    }
}

void SimpleMap::map_scroll_event_cb(lv_event_t *e)
{
    if (!initialized || !map_handle) return;
    
    lv_event_code_t event_code = lv_event_get_code(e);

    if (programmatic_scroll) return;

    if (event_code == LV_EVENT_SCROLL_BEGIN) {
        if (gps_follow_enabled) {
            gps_follow_enabled = false;
            update_gps_status();
            printf("SimpleMap: GPS follow paused by user scroll\n");
        }
        return;
    }
    
    // Keep the geographic center current while dragging, but defer text-area
    // redraws until release to avoid overlay invalidations on every frame.
    if (event_code == LV_EVENT_SCROLL) {
        uint32_t current_time = esp_timer_get_time() / 1000;
        if (current_time - last_gps_update_time >= 50) {
            update_current_gps_from_map_center(false);
            last_gps_update_time = current_time;
        }
        return;
    }
    
    // Handle tile loading when scroll ends
    if (event_code == LV_EVENT_SCROLL_END) {
        if (drag_active) return;

        // Don't process scroll events if already loading
        if (is_loading) {
            return;
        }
        
        constexpr int32_t kReloadOverscroll = 90;
        int32_t left = lv_obj_get_scroll_left(map_container);
        int32_t right = lv_obj_get_scroll_right(map_container);
        int32_t top = lv_obj_get_scroll_top(map_container);
        int32_t bottom = lv_obj_get_scroll_bottom(map_container);
        bool crossed_grid_edge = left < -kReloadOverscroll || right < -kReloadOverscroll ||
                                 top < -kReloadOverscroll || bottom < -kReloadOverscroll;

        // Capture the geographic center before changing the grid origin. This
        // handles horizontal and vertical edge crossings in the same gesture.
        update_current_gps_from_map_center();
        if (crossed_grid_edge) {
            map_tiles_set_center_from_gps(map_handle, current_lat, current_lon);
            pending_drag_center = true;
            load_map_tiles();
        } else {
            // Return a small elastic drag to the nearest natural boundary.
            int32_t scroll_x = lv_obj_get_scroll_x(map_container);
            int32_t scroll_y = lv_obj_get_scroll_y(map_container);
            if (left < 0) scroll_x = 0;
            else if (right < 0) scroll_x += right;
            if (top < 0) scroll_y = 0;
            else if (bottom < 0) scroll_y += bottom;

            programmatic_scroll = true;
            lv_obj_scroll_to(map_container, scroll_x, scroll_y, LV_ANIM_OFF);
            programmatic_scroll = false;
        }
    }
}

void SimpleMap::gps_follow_event_cb(lv_event_t *e)
{
    (void)e;
    set_gps_follow(true);
}

void SimpleMap::update_gps_marker()
{
    if (!gps_marker || !gps_fix_received || !map_handle) return;

    double tile_x = 0.0;
    double tile_y = 0.0;
    map_tiles_gps_to_tile_xy(map_handle, gps_lat, gps_lon, &tile_x, &tile_y);

    int grid_x = 0;
    int grid_y = 0;
    map_tiles_get_position(map_handle, &grid_x, &grid_y);

    int pixel_x = (int)((tile_x - grid_x) * MAP_TILES_TILE_SIZE);
    int pixel_y = (int)((tile_y - grid_y) * MAP_TILES_TILE_SIZE);
    lv_obj_set_pos(gps_marker, pixel_x - 11, pixel_y - 11);
    lv_obj_set_style_bg_color(gps_marker,
                              gps_fix_valid ? lv_palette_main(LV_PALETTE_BLUE) : lv_palette_main(LV_PALETTE_GREY), 0);
    if (gps_heading_line && gps_heading_valid) {
        const float radians = gps_heading_degrees * (3.14159265358979323846f / 180.0f);
        gps_heading_points[1].x = 11 + (int32_t)lroundf(sinf(radians) * 8.0f);
        gps_heading_points[1].y = 11 - (int32_t)lroundf(cosf(radians) * 8.0f);
        lv_line_set_points_mutable(gps_heading_line, gps_heading_points, 2);
        lv_obj_clear_flag(gps_heading_line, LV_OBJ_FLAG_HIDDEN);
    } else if (gps_heading_line) {
        lv_obj_add_flag(gps_heading_line, LV_OBJ_FLAG_HIDDEN);
    }
    lv_obj_clear_flag(gps_marker, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(gps_marker);
}

void SimpleMap::update_gps_status()
{
    if (gps_status_label) {
        if (gps_fix_valid) {
            lv_label_set_text_fmt(gps_status_label, LV_SYMBOL_GPS " %u sat%s", gps_satellites,
                                  gps_follow_enabled ? "  FOLLOW" : "");
        } else {
            lv_label_set_text_fmt(gps_status_label, LV_SYMBOL_GPS " No fix  %u sat", gps_satellites);
        }
    }

    if (gps_follow_button) {
        lv_obj_set_style_bg_color(gps_follow_button,
                                  gps_follow_enabled ? lv_palette_main(LV_PALETTE_BLUE)
                                                     : lv_palette_main(LV_PALETTE_GREY), 0);
    }

    update_gps_marker();
}

bool SimpleMap::gps_near_tile_grid_edge()
{
    if (!gps_fix_received || !map_handle) return false;

    double gps_tile_x = 0.0;
    double gps_tile_y = 0.0;
    map_tiles_gps_to_tile_xy(map_handle, gps_lat, gps_lon, &gps_tile_x, &gps_tile_y);

    int grid_x = 0;
    int grid_y = 0;
    map_tiles_get_position(map_handle, &grid_x, &grid_y);
    int tile_x = (int)gps_tile_x;
    int tile_y = (int)gps_tile_y;

    return tile_x <= grid_x || tile_y <= grid_y ||
           tile_x >= grid_x + grid_cols - 1 || tile_y >= grid_y + grid_rows - 1;
}

void SimpleMap::create_input_panel(lv_obj_t* parent_screen)
{
    // Create input panel container - increased height for button
    input_panel = lv_obj_create(parent_screen);
    lv_obj_set_size(input_panel, 220, 180);
    lv_obj_align(input_panel, LV_ALIGN_LEFT_MID, 20, 0);
    lv_obj_set_style_bg_color(input_panel, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(input_panel, LV_OPA_30, 0);
    lv_obj_set_style_border_width(input_panel, 1, 0);
    lv_obj_set_style_border_color(input_panel, lv_color_white(), 0);
    lv_obj_set_style_radius(input_panel, 5, 0);
    lv_obj_set_style_pad_all(input_panel, 8, 0);

    // Create latitude label and textarea
    lv_obj_t* lat_label = lv_label_create(input_panel);
    lv_label_set_text(lat_label, "Latitude:");
    lv_obj_set_style_text_color(lat_label, lv_color_white(), 0);
    lv_obj_align(lat_label, LV_ALIGN_TOP_LEFT, 0, 10);

    lat_textarea = lv_textarea_create(input_panel);
    lv_obj_set_size(lat_textarea, 110, 25);
    lv_obj_align(lat_textarea, LV_ALIGN_TOP_RIGHT, 0, 0);
    lv_textarea_set_one_line(lat_textarea, true);
    lv_textarea_set_text(lat_textarea, "0.000000");
    lv_obj_add_event_cb(lat_textarea, textarea_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(lat_textarea, textarea_event_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(lat_textarea, textarea_event_cb, LV_EVENT_READY, NULL);
    lv_obj_set_style_anim_time(lat_textarea, 0, LV_PART_CURSOR);

    // Create longitude label and textarea
    lv_obj_t* lon_label = lv_label_create(input_panel);
    lv_label_set_text(lon_label, "Longitude:");
    lv_obj_set_style_text_color(lon_label, lv_color_white(), 0);
    lv_obj_align_to(lon_label, lat_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 30);

    lon_textarea = lv_textarea_create(input_panel);
    lv_obj_set_size(lon_textarea, 110, 25);
    lv_obj_align_to(lon_textarea, lat_textarea, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_textarea_set_one_line(lon_textarea, true);
    lv_textarea_set_text(lon_textarea, "0.000000");
    lv_obj_add_event_cb(lon_textarea, textarea_event_cb, LV_EVENT_FOCUSED, NULL);
    lv_obj_add_event_cb(lon_textarea, textarea_event_cb, LV_EVENT_DEFOCUSED, NULL);
    lv_obj_add_event_cb(lon_textarea, textarea_event_cb, LV_EVENT_READY, NULL);
    lv_obj_set_style_anim_time(lon_textarea, 0, LV_PART_CURSOR);

    // Create zoom label and slider
    zoom_label = lv_label_create(input_panel);
    lv_label_set_text_fmt(zoom_label, "Zoom: (%d)", current_zoom);
    lv_obj_set_style_text_color(zoom_label, lv_color_white(), 0);
    lv_obj_align_to(zoom_label, lon_label, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 20);

    zoom_slider = lv_slider_create(input_panel);
    lv_obj_set_size(zoom_slider, 100, 10);
    lv_obj_align_to(zoom_slider, lon_textarea, LV_ALIGN_OUT_BOTTOM_MID, 0, 10);
    lv_slider_set_range(zoom_slider, min_zoom, max_zoom);
    lv_slider_set_value(zoom_slider, current_zoom, LV_ANIM_OFF);
    lv_obj_add_event_cb(zoom_slider, zoom_slider_event_cb, LV_EVENT_VALUE_CHANGED, NULL);

    // Create update button
    update_button = lv_btn_create(input_panel);
    lv_obj_set_size(update_button, 150, 30);
    lv_obj_align(update_button, LV_ALIGN_BOTTOM_MID, 0, -10);
    lv_obj_add_event_cb(update_button, update_button_event_cb, LV_EVENT_CLICKED, NULL);
    
    // Button styling
    lv_obj_set_style_bg_color(update_button, lv_palette_main(LV_PALETTE_BLUE), 0);
    lv_obj_set_style_bg_opa(update_button, LV_OPA_COVER, 0);
    
    // Button label
    lv_obj_t* btn_label = lv_label_create(update_button);
    lv_label_set_text(btn_label, "Update Map");
    lv_obj_set_style_text_color(btn_label, lv_color_white(), 0);
    lv_obj_center(btn_label);

    // Create keyboard (initially hidden)
    keyboard = lv_keyboard_create(lv_screen_active());
    lv_obj_set_size(keyboard, LV_HOR_RES, LV_VER_RES / 2);
    lv_obj_align(keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(keyboard, LV_KEYBOARD_MODE_NUMBER);  // Set to number layout
    lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);  // Start hidden
}

void SimpleMap::zoom_slider_event_cb(lv_event_t *e)
{
    lv_obj_t* slider = (lv_obj_t*) lv_event_get_target(e);
    int selected_zoom = lv_slider_get_value(slider);
    
    // Update zoom label to show the selected value (not yet applied)
    if (zoom_label) {
        int component_zoom = map_tiles_get_zoom(map_handle);
        if (selected_zoom != component_zoom) {
            // Show that this is a selected but not yet applied zoom level
            lv_label_set_text_fmt(zoom_label, "Zoom: (%d*)", selected_zoom);
        } else {
            // Show that this zoom level is currently applied
            lv_label_set_text_fmt(zoom_label, "Zoom: (%d)", selected_zoom);
        }
    }
    
    printf("SimpleMap: Zoom slider changed to %d (use Update button to apply)\n", selected_zoom);
}

void SimpleMap::update_zoom_label()
{
    if (zoom_label) {
        // Show the actual applied zoom level (no asterisk means it's applied)
        lv_label_set_text_fmt(zoom_label, "Zoom: (%d)", current_zoom);
    }
}

void SimpleMap::textarea_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    lv_obj_t* textarea = (lv_obj_t*)lv_event_get_target(e);
    
    if (code == LV_EVENT_FOCUSED) {
        // Show keyboard when textarea is focused
        if (keyboard) {
            lv_keyboard_set_textarea(keyboard, textarea);
            lv_obj_clear_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            lv_obj_move_foreground(keyboard);
            printf("SimpleMap: Keyboard shown for textarea\n");
        }
    }
    else if (code == LV_EVENT_DEFOCUSED) {
        // Hide keyboard when textarea loses focus
        if (keyboard) {
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            printf("SimpleMap: Keyboard hidden\n");
        }
        
        // Validate and update coordinates when focus is lost
        const char* text = lv_textarea_get_text(textarea);
        
        if (textarea == lat_textarea) {
            double lat_val = atof(text);
            if (lat_val >= -90.0 && lat_val <= 90.0) {
                current_lat = lat_val;
                printf("SimpleMap: Latitude updated to %f\n", lat_val);
            }
        } else if (textarea == lon_textarea) {
            double lon_val = atof(text);
            if (lon_val >= -180.0 && lon_val <= 180.0) {
                current_lon = lon_val;
                printf("SimpleMap: Longitude updated to %f\n", lon_val);
            }
        }
    }
    else if (code == LV_EVENT_READY) {
        // Handle keyboard ready event (when user presses Enter or Done)
        if (keyboard) {
            lv_obj_add_flag(keyboard, LV_OBJ_FLAG_HIDDEN);
            printf("SimpleMap: Keyboard hidden via READY event\n");
        }
        
        // Validate and update coordinates
        const char* text = lv_textarea_get_text(textarea);
        
        if (textarea == lat_textarea) {
            double lat_val = atof(text);
            if (lat_val >= -90.0 && lat_val <= 90.0) {
                current_lat = lat_val;
                printf("SimpleMap: Latitude ready: %f\n", lat_val);
            }
        } else if (textarea == lon_textarea) {
            double lon_val = atof(text);
            if (lon_val >= -180.0 && lon_val <= 180.0) {
                current_lon = lon_val;
                printf("SimpleMap: Longitude ready: %f\n", lon_val);
            }
        }
    }
}

void SimpleMap::update_button_event_cb(lv_event_t *e)
{
    printf("SimpleMap: Update button clicked\n");
    set_gps_follow(false);
    
    // Get values from input fields
    double new_lat = current_lat;
    double new_lon = current_lon;
    int new_zoom = current_zoom;
    bool valid_input = true;
    bool zoom_changed = false;
    
    // Get latitude from textarea
    if (lat_textarea) {
        const char* lat_text = lv_textarea_get_text(lat_textarea);
        double lat_val = atof(lat_text);
        if (lat_val >= -90.0 && lat_val <= 90.0) {
            new_lat = lat_val;
        } else {
            printf("SimpleMap: Invalid latitude: %s\n", lat_text);
            valid_input = false;
        }
    }
    
    // Get longitude from textarea
    if (lon_textarea) {
        const char* lon_text = lv_textarea_get_text(lon_textarea);
        double lon_val = atof(lon_text);
        if (lon_val >= -180.0 && lon_val <= 180.0) {
            new_lon = lon_val;
        } else {
            printf("SimpleMap: Invalid longitude: %s\n", lon_text);
            valid_input = false;
        }
    }
    
    // Get zoom from slider
    if (zoom_slider) {
        int slider_zoom = lv_slider_get_value(zoom_slider);
        // Check if zoom changed by comparing with the actual zoom level in the map tiles component
        int component_zoom = map_tiles_get_zoom(map_handle);
        if (slider_zoom != component_zoom) {
            zoom_changed = true;
        }
        new_zoom = slider_zoom;
    }
    
    // Update map if all inputs are valid
    if (valid_input) {
        printf("SimpleMap: Updating map to lat=%.6f, lon=%.6f, zoom=%d\n", new_lat, new_lon, new_zoom);
        
        // Update coordinates first
        current_lat = new_lat;
        current_lon = new_lon;
        
        // Update input panel to show the new coordinates
        if (lat_textarea) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.6f", current_lat);
            lv_textarea_set_text(lat_textarea, buf);
        }
        if (lon_textarea) {
            char buf[32];
            snprintf(buf, sizeof(buf), "%.6f", current_lon);
            lv_textarea_set_text(lon_textarea, buf);
        }
        
        if (zoom_changed) {
            // Change zoom level and reload tiles
            change_zoom_level(new_zoom);
        } else {
            // Just update the map position without changing zoom
            map_tiles_set_center_from_gps(map_handle, new_lat, new_lon);
            
            // Check if we need to load new tiles for the new position
            if (!map_tiles_is_gps_within_tiles(map_handle, new_lat, new_lon)) {
                load_map_tiles();
            }
            
            center_map_on_gps();
        }
    } else {
        printf("SimpleMap: Invalid input - map not updated\n");
    }
}

void SimpleMap::show_loading_popup()
{
    if (loading_popup) return;
   
    // Thin fullscreen overlay (light opacity)
    loading_popup = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(loading_popup);
    lv_obj_set_size(loading_popup, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(loading_popup, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(loading_popup, LV_OPA_40, 0); // lighter dim
    lv_obj_set_style_border_width(loading_popup, 0, 0);

    // Small pill card centered
    lv_obj_t* card = lv_obj_create(loading_popup);
    lv_obj_remove_style_all(card);
    lv_obj_set_size(card, 170, 42);
    lv_obj_center(card);

    // Simple, high-contrast look (no shadow/gradient)
    lv_obj_set_style_radius(card, 8, 0);
    lv_obj_set_style_bg_color(card, lv_palette_main(LV_PALETTE_YELLOW), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_90, 0);
    lv_obj_set_style_border_width(card, 1, 0);
    lv_obj_set_style_border_color(card, lv_palette_darken(LV_PALETTE_GREY, 2), 0);
    lv_obj_set_style_pad_all(card, 8, 0);

    // Single static label
    lv_obj_t* label = lv_label_create(card);
    lv_label_set_text(label, "Loading map...");
    lv_obj_set_style_text_color(label, lv_color_black(), 0);
    lv_obj_center(label);

    lv_obj_move_foreground(loading_popup);
}

void SimpleMap::hide_loading_popup()
{
    if (loading_popup) {
        lv_obj_delete(loading_popup);
        loading_popup = nullptr;
    }
}

void SimpleMap::center_map_on_gps()
{
    if (!initialized || !map_handle || !map_group || !map_container) {
        printf("SimpleMap: Map not properly initialized for centering\n");
        return;
    }

    // Convert current GPS coordinates to tile coordinates
    double x, y;
    map_tiles_gps_to_tile_xy(map_handle, current_lat, current_lon, &x, &y);
    
    // Get current tile position (top-left of the 5x5 grid)
    int tile_x, tile_y;
    map_tiles_get_position(map_handle, &tile_x, &tile_y);
    
    // Calculate absolute pixel position of GPS coordinates
    int abs_px = (int)(x * MAP_TILES_TILE_SIZE);
    int abs_py = (int)(y * MAP_TILES_TILE_SIZE);
    
    // Calculate top-left pixel position of current tile grid
    int top_left_px_x = tile_x * MAP_TILES_TILE_SIZE;
    int top_left_px_y = tile_y * MAP_TILES_TILE_SIZE;
    
    // Calculate scroll position to center GPS coordinates in the map container
    int center_scroll_x = abs_px - top_left_px_x - lv_obj_get_width(map_container) / 2;
    int center_scroll_y = abs_py - top_left_px_y - lv_obj_get_height(map_container) / 2;
    
    // Apply the scroll to center the GPS coordinates
    programmatic_scroll = true;
    lv_obj_scroll_to(map_container, center_scroll_x, center_scroll_y, LV_ANIM_OFF);
    programmatic_scroll = false;
    
    printf("SimpleMap: Centered map on GPS %.6f, %.6f (scroll: %d, %d)\n", 
           current_lat, current_lon, center_scroll_x, center_scroll_y);
}

void SimpleMap::update_current_gps_from_map_center(bool update_widgets)
{
    if (!initialized || !map_handle || !map_container) {
        return;
    }
    
    // Get current scroll position
    lv_coord_t scroll_x = lv_obj_get_scroll_x(map_container);
    lv_coord_t scroll_y = lv_obj_get_scroll_y(map_container);
    
    // Get screen center offset
    lv_coord_t screen_center_x = lv_obj_get_width(map_container) / 2;
    lv_coord_t screen_center_y = lv_obj_get_height(map_container) / 2;
    
    // Calculate the center pixel position relative to the map
    int center_pixel_x = scroll_x + screen_center_x;
    int center_pixel_y = scroll_y + screen_center_y;
    
    // Get current tile position (top-left of the grid)
    int tile_x, tile_y;
    map_tiles_get_position(map_handle, &tile_x, &tile_y);
    
    // Calculate top-left pixel position of current tile grid
    int top_left_px_x = tile_x * MAP_TILES_TILE_SIZE;
    int top_left_px_y = tile_y * MAP_TILES_TILE_SIZE;
    
    // Calculate absolute pixel position
    int abs_pixel_x = top_left_px_x + center_pixel_x;
    int abs_pixel_y = top_left_px_y + center_pixel_y;
    
    // Convert absolute pixel position to tile coordinates
    double tile_coord_x = (double)abs_pixel_x / MAP_TILES_TILE_SIZE;
    double tile_coord_y = (double)abs_pixel_y / MAP_TILES_TILE_SIZE;
    
    // Convert tile coordinates to GPS coordinates
    double new_lat, new_lon;
    map_tiles_tile_xy_to_gps(map_handle, tile_coord_x, tile_coord_y, &new_lat, &new_lon);
    
    // Update current GPS coordinates
    current_lat = new_lat;
    current_lon = new_lon;
    
    // Updating the text areas invalidates their overlay. Avoid doing that on
    // every pointer sample; the release path refreshes them once at the end.
    if (update_widgets && lat_textarea) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", current_lat);
        lv_textarea_set_text(lat_textarea, buf);
    }
    if (update_widgets && lon_textarea) {
        char buf[32];
        snprintf(buf, sizeof(buf), "%.6f", current_lon);
        lv_textarea_set_text(lon_textarea, buf);
    }
    
    // GPS coordinates updated silently for better performance
}

void SimpleMap::change_zoom_level(int new_zoom) {
    if (!initialized || !map_handle) {
        printf("SimpleMap: Map not initialized, cannot change zoom\n");
        return;
    }

    if (new_zoom < min_zoom || new_zoom > max_zoom) {
        printf("SimpleMap: Zoom %d outside configured range %d..%d\n", new_zoom, min_zoom, max_zoom);
        return;
    }
    
    if (new_zoom == current_zoom) {
        printf("SimpleMap: Zoom level already set to %d\n", new_zoom);
        return;
    }
    
    printf("SimpleMap: Changing zoom from %d to %d\n", current_zoom, new_zoom);
    
    // Set zoom level in the component first
    map_tiles_set_zoom(map_handle, new_zoom);
    
    // Update current zoom after setting it in the component
    current_zoom = new_zoom;
    
    // Update zoom slider to reflect the change
    if (zoom_slider) {
        lv_slider_set_value(zoom_slider, new_zoom, LV_ANIM_OFF);
    }
    
    // Update zoom label
    update_zoom_label();
    
    // Recalculate tile positions for the new zoom level centered on current GPS coordinates
    map_tiles_set_center_from_gps(map_handle, current_lat, current_lon);
    
    // Force reload all tiles for the new zoom level
    load_map_tiles();
    
    // Center the map view after tiles are loaded
    lv_timer_create([](lv_timer_t *t) {
        lv_timer_del(t);
        center_map_on_gps();
        printf("SimpleMap: Zoom change completed, map centered\n");
    }, 200, NULL);
}

void SimpleMap::get_current_location(double* latitude, double* longitude) {
    if (latitude) *latitude = current_lat;
    if (longitude) *longitude = current_lon;
}

int SimpleMap::get_current_zoom() {
    return current_zoom;
}

void SimpleMap::cleanup() {
    if (map_container) {
        lv_obj_delete(map_container);
        map_container = nullptr;
    }
    gps_marker = nullptr;
    gps_heading_line = nullptr;
    
    if (input_panel) {
        lv_obj_delete(input_panel);
        input_panel = nullptr;
    }
    
    if (keyboard) {
        lv_obj_delete(keyboard);
        keyboard = nullptr;
    }
    
    if (loading_popup) {
        lv_obj_delete(loading_popup);
        loading_popup = nullptr;
    }

    if (gps_status_label) {
        lv_obj_delete(gps_status_label);
        gps_status_label = nullptr;
    }

    if (gps_follow_button) {
        lv_obj_delete(gps_follow_button);
        gps_follow_button = nullptr;
    }
    
    map_group = nullptr;  // Will be cleaned up when map_container is deleted
    lat_textarea = nullptr;
    lon_textarea = nullptr;
    zoom_slider = nullptr;
    zoom_label = nullptr;
    update_button = nullptr;
    
    // Clean up the map tiles component
    if (map_handle) {
        map_tiles_cleanup(map_handle);
        map_handle = nullptr;
    }
    
    // Clean up tile widgets array
    if (tile_widgets) {
        free(tile_widgets);
        tile_widgets = nullptr;
    }
    
    // Reset cached grid dimensions
    grid_cols = 0;
    grid_rows = 0;
    tile_count = 0;
    
    // Reset loading flag
    is_loading = false;
    gps_fix_received = false;
    gps_fix_valid = false;
    gps_follow_enabled = true;
    programmatic_scroll = false;
    pending_follow_center = false;
    pending_drag_center = false;
    drag_active = false;
    gps_satellites = 0;
    gps_heading_degrees = 0.0f;
    gps_heading_valid = false;
    gps_lat = 0.0;
    gps_lon = 0.0;
    last_scroll_time = 0;
    last_gps_update_time = 0;
    
    initialized = false;
}
