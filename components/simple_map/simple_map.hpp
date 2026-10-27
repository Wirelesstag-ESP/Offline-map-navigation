#pragma once

#include "lvgl.h"
#include "map_tiles.h"

class SimpleMap {
public:
    // Initialize the simple map system
    static bool init(lv_obj_t* parent_screen);

    // Configure the available tile zoom range before initialization
    static bool set_zoom_range(int min_zoom, int max_zoom);
    
    // Display map at specific coordinates
    static void show_location(double latitude, double longitude, int zoom_level = 18);
    
    // Update location (for moving GPS)
    static void update_location(double latitude, double longitude);

    // Update the latest GNSS fix and on-screen tracking state
    static void update_gps_fix(double latitude, double longitude, uint8_t satellites, bool valid);

    // Update the device heading used by the GPS direction marker
    static void update_heading(float heading_degrees, bool valid);

    // Enable or pause automatic map following
    static void set_gps_follow(bool enabled);
    static bool is_gps_following();
    
    // Toggle between map and satellite view
    static void set_satellite_view(bool satellite);
    
    // Set specific tile type (0 = tiles1, 1 = tiles2, etc.)
    static bool set_tile_type(int tile_type);
    
    // Get current tile type
    static int get_tile_type();
    
    // Get available tile type count
    static int get_tile_type_count();
    
    // Get tile type folder name
    static const char* get_tile_type_folder(int tile_type);
    
    // Center map on current GPS coordinates with pixel precision
    static void center_map_on_gps();
    
    // Change zoom level and reload map tiles
    static void change_zoom_level(int new_zoom);
    
    // Get current GPS coordinates (latitude, longitude) at center of screen
    static void get_current_location(double* latitude, double* longitude);
    
    // Get current zoom level
    static int get_current_zoom();
    
    // Cleanup
    static void cleanup();

private:
    static lv_obj_t* map_container;
    static lv_obj_t* map_group;
    static lv_obj_t** tile_widgets;
    static int grid_cols, grid_rows, tile_count;  // Cache grid dimensions
    static lv_obj_t* input_panel;
    static lv_obj_t* lat_textarea;
    static lv_obj_t* lon_textarea;
    static lv_obj_t* zoom_slider;
    static lv_obj_t* zoom_label;
    static lv_obj_t* update_button;
    static lv_obj_t* keyboard;
    static lv_obj_t* loading_popup;
    static lv_obj_t* gps_marker;
    static lv_obj_t* gps_heading_line;
    static lv_point_precise_t gps_heading_points[2];
    static lv_obj_t* gps_status_label;
    static lv_obj_t* gps_follow_button;
    static double current_lat;
    static double current_lon;
    static double gps_lat;
    static double gps_lon;
    static int current_zoom;
    static int min_zoom;
    static int max_zoom;
    static bool initialized;
    static bool is_loading;  // Flag to prevent multiple simultaneous loads
    static bool gps_fix_received;
    static bool gps_fix_valid;
    static bool gps_follow_enabled;
    static bool programmatic_scroll;
    static bool pending_follow_center;
    static bool pending_drag_center;
    static bool drag_active;
    static uint8_t gps_satellites;
    static float gps_heading_degrees;
    static bool gps_heading_valid;
    static uint32_t last_scroll_time;  // Timestamp of last scroll event
    static uint32_t last_gps_update_time;  // Timestamp of last GPS update
    static map_tiles_handle_t map_handle;
    
    // Helper functions
    static void load_map_tiles();
    static void create_tile_widgets();
    static void create_gps_controls(lv_obj_t* parent_screen);
    static void map_drag_event_cb(lv_event_t *e);
    static void map_scroll_event_cb(lv_event_t *e);
    static void gps_follow_event_cb(lv_event_t *e);
    static void create_input_panel(lv_obj_t* parent_screen);
    static void zoom_slider_event_cb(lv_event_t *e);
    static void textarea_event_cb(lv_event_t *e);
    static void update_button_event_cb(lv_event_t *e);
    static void update_zoom_label();
    static void show_loading_popup();
    static void hide_loading_popup();
    static void update_gps_marker();
    static void update_gps_status();
    static bool gps_near_tile_grid_edge();
    static void update_current_gps_from_map_center(bool update_widgets = true);
};
