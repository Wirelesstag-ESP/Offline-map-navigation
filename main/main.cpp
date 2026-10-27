#include <atomic>
#include <cerrno>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/stat.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs_flash.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_spiffs.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "bsp/esp-bsp.h"
#include "bno085_attitude.h"
#include "lvgl.h"
#include "gps_lc76g.h"
#include "simple_map.hpp"

namespace {

constexpr int kInternalMinZoom = 9;
constexpr int kInternalMaxZoom = 10;
constexpr int kInternalInitialZoom = 10;
constexpr int kSdMinZoom = 2;
constexpr int kSdMaxZoom = 11;
constexpr int kSdInitialZoom = 10;
constexpr char kFallbackLatitude[] = "30.749125";
constexpr char kFallbackLongitude[] = "103.931633";
constexpr char kMapTileRoot[] = "/sdcard/tiles1";
constexpr char kMapProbeTile[] = "/sdcard/tiles1/10/834/404.bin";
constexpr uint32_t kGpsPollMs = 500;
constexpr uint32_t kGpsStaleMs = 5000;
constexpr uint32_t kAttitudePollMs = 200;
constexpr uint32_t kAttitudeUiUpdateMs = 500;

const char *TAG = "simple_map_app";
std::atomic<bool> s_map_started{false};
lv_obj_t *s_startup_screen = nullptr;
lv_obj_t *s_status_label = nullptr;
lv_obj_t *s_lat_textarea = nullptr;
lv_obj_t *s_lon_textarea = nullptr;
lv_obj_t *s_keyboard = nullptr;
lv_obj_t *s_attitude_status_label = nullptr;
lv_obj_t *s_attitude_cal_button = nullptr;
bool s_gps_available = false;
bool s_attitude_available = false;
uint32_t s_attitude_message_until_ms = 0;
uint32_t s_attitude_ui_last_update_ms = 0;
int s_map_min_zoom = kInternalMinZoom;
int s_map_max_zoom = kInternalMaxZoom;
int s_map_initial_zoom = kInternalInitialZoom;

void create_attitude_controls(lv_obj_t *parent_screen);

esp_err_t mount_map_storage()
{
    esp_err_t ret = bsp_sdcard_mount();
    if (ret == ESP_OK) {
        struct stat tile_root = {};
        if (stat(kMapTileRoot, &tile_root) == 0 && S_ISDIR(tile_root.st_mode)) {
            s_map_min_zoom = kSdMinZoom;
            s_map_max_zoom = kSdMaxZoom;
            s_map_initial_zoom = kSdInitialZoom;
            ESP_LOGI(TAG, "SD card map storage mounted at %s (zoom %d..%d, initial %d)",
                     BSP_SD_MOUNT_POINT, s_map_min_zoom, s_map_max_zoom, s_map_initial_zoom);
            return ESP_OK;
        }

        ESP_LOGW(TAG, "SD card has no %s directory; using internal map storage", kMapTileRoot);
        ret = bsp_sdcard_unmount();
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Could not unmount empty SD card: %s", esp_err_to_name(ret));
            return ret;
        }
    } else {
        ESP_LOGW(TAG, "SD card mount failed: %s; trying internal map storage", esp_err_to_name(ret));
    }

    const esp_vfs_spiffs_conf_t spiffs_config = {
        .base_path = BSP_SD_MOUNT_POINT,
        .partition_label = "storage",
        .max_files = 8,
        .format_if_mount_failed = false,
    };
    ret = esp_vfs_spiffs_register(&spiffs_config);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Internal map storage mount failed: %s", esp_err_to_name(ret));
        return ret;
    }

    size_t total_bytes = 0;
    size_t used_bytes = 0;
    ret = esp_spiffs_info(spiffs_config.partition_label, &total_bytes, &used_bytes);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Internal map storage info failed: %s", esp_err_to_name(ret));
        esp_vfs_spiffs_unregister(spiffs_config.partition_label);
        return ret;
    }

    ESP_LOGI(TAG, "Internal map storage mounted at %s (%u/%u bytes used)", BSP_SD_MOUNT_POINT,
             static_cast<unsigned>(used_bytes), static_cast<unsigned>(total_bytes));

    struct stat probe_tile = {};
    if (stat(kMapProbeTile, &probe_tile) != 0 || probe_tile.st_size != 131084) {
        ESP_LOGE(TAG, "Internal map probe tile is missing or invalid: %s", kMapProbeTile);
        esp_vfs_spiffs_unregister(spiffs_config.partition_label);
        return ESP_ERR_NOT_FOUND;
    }

    s_map_min_zoom = kInternalMinZoom;
    s_map_max_zoom = kInternalMaxZoom;
    s_map_initial_zoom = kInternalInitialZoom;
    ESP_LOGI(TAG, "Internal map probe tile ready: %s (zoom %d..%d, initial %d)", kMapProbeTile,
             s_map_min_zoom, s_map_max_zoom, s_map_initial_zoom);
    return ESP_OK;
}

bool parse_coordinate(const char *text, double min_value, double max_value, double *value)
{
    if (!text || !value) {
        return false;
    }

    errno = 0;
    char *end = nullptr;
    double parsed = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' || !std::isfinite(parsed) || parsed < min_value ||
        parsed > max_value) {
        return false;
    }

    *value = parsed;
    return true;
}

bool start_map(double latitude, double longitude)
{
    bool expected = false;
    if (!s_map_started.compare_exchange_strong(expected, true)) {
        return true;
    }

    ESP_LOGI(TAG, "Starting map at %.7f, %.7f (zoom %d)", latitude, longitude, s_map_initial_zoom);

    if (s_startup_screen) {
        lv_obj_delete(s_startup_screen);
        s_startup_screen = nullptr;
        s_status_label = nullptr;
        s_lat_textarea = nullptr;
        s_lon_textarea = nullptr;
        s_keyboard = nullptr;
    }

    if (!SimpleMap::set_zoom_range(s_map_min_zoom, s_map_max_zoom)) {
        ESP_LOGE(TAG, "Invalid map zoom range %d..%d", s_map_min_zoom, s_map_max_zoom);
        s_map_started.store(false);
        return false;
    }

    if (!SimpleMap::init(lv_screen_active())) {
        ESP_LOGE(TAG, "SimpleMap initialization failed");
        s_map_started.store(false);
        return false;
    }

    create_attitude_controls(lv_screen_active());
    SimpleMap::show_location(latitude, longitude, s_map_initial_zoom);
    SimpleMap::center_map_on_gps();
    return true;
}

void heading_calibration_event_cb(lv_event_t *event)
{
    (void)event;
    esp_err_t ret = bno085_attitude_set_heading_zero();
    if (s_attitude_status_label) {
        if (ret == ESP_OK) {
            lv_label_set_text(s_attitude_status_label, "Heading zero set");
        } else {
            lv_label_set_text(s_attitude_status_label, "IMU not ready");
        }
        s_attitude_message_until_ms = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL) + 2000;
    }
}

void create_attitude_controls(lv_obj_t *parent_screen)
{
    s_attitude_status_label = lv_label_create(parent_screen);
    lv_label_set_text(s_attitude_status_label, s_attitude_available ? "IMU waiting" : "IMU unavailable");
    lv_obj_set_style_text_color(s_attitude_status_label, lv_color_white(), 0);
    lv_obj_set_style_bg_color(s_attitude_status_label, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_attitude_status_label, LV_OPA_60, 0);
    lv_obj_set_style_pad_hor(s_attitude_status_label, 8, 0);
    lv_obj_set_style_pad_ver(s_attitude_status_label, 5, 0);
    lv_obj_set_style_radius(s_attitude_status_label, 4, 0);
    lv_obj_align(s_attitude_status_label, LV_ALIGN_TOP_RIGHT, -16, 52);

    s_attitude_cal_button = lv_button_create(parent_screen);
    lv_obj_set_size(s_attitude_cal_button, 44, 44);
    lv_obj_align(s_attitude_cal_button, LV_ALIGN_BOTTOM_RIGHT, -16, -68);
    lv_obj_set_style_radius(s_attitude_cal_button, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_attitude_cal_button,
                              s_attitude_available ? lv_palette_main(LV_PALETTE_BLUE)
                                                   : lv_palette_main(LV_PALETTE_GREY), 0);
    if (!s_attitude_available) {
        lv_obj_add_state(s_attitude_cal_button, LV_STATE_DISABLED);
    }
    lv_obj_add_event_cb(s_attitude_cal_button, heading_calibration_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *icon = lv_label_create(s_attitude_cal_button);
    lv_label_set_text(icon, LV_SYMBOL_REFRESH);
    lv_obj_center(icon);
}

void textarea_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    auto *textarea = static_cast<lv_obj_t *>(lv_event_get_target(event));

    if (code == LV_EVENT_FOCUSED) {
        lv_keyboard_set_textarea(s_keyboard, textarea);
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_keyboard);
    } else if (code == LV_EVENT_READY || code == LV_EVENT_CANCEL) {
        lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_state(textarea, LV_STATE_FOCUSED);
        lv_indev_reset(nullptr, textarea);
    }
}

void manual_start_event_cb(lv_event_t *event)
{
    (void)event;
    double latitude = 0.0;
    double longitude = 0.0;
    bool valid = parse_coordinate(lv_textarea_get_text(s_lat_textarea), -90.0, 90.0, &latitude) &&
                 parse_coordinate(lv_textarea_get_text(s_lon_textarea), -180.0, 180.0, &longitude);

    if (!valid) {
        lv_label_set_text(s_status_label, "Invalid coordinates");
        return;
    }

    start_map(latitude, longitude);
}

void create_startup_screen()
{
    s_startup_screen = lv_obj_create(lv_screen_active());
    lv_obj_set_size(s_startup_screen, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_startup_screen, lv_color_hex(0x101418), 0);
    lv_obj_set_style_border_width(s_startup_screen, 0, 0);
    lv_obj_set_style_radius(s_startup_screen, 0, 0);
    lv_obj_clear_flag(s_startup_screen, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *panel = lv_obj_create(s_startup_screen);
    lv_obj_set_size(panel, 440, 300);
    lv_obj_center(panel);
    lv_obj_set_style_radius(panel, 6, 0);
    lv_obj_set_style_bg_color(panel, lv_color_hex(0x20272D), 0);
    lv_obj_set_style_border_color(panel, lv_color_hex(0x56616A), 0);
    lv_obj_set_style_pad_all(panel, 20, 0);

    lv_obj_t *title = lv_label_create(panel);
    lv_label_set_text(title, "Simple Map");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 0);

    s_status_label = lv_label_create(panel);
    lv_label_set_text(s_status_label,
                      s_gps_available ? "Waiting for LC76G fix..." : "LC76G unavailable; enter coordinates");
    lv_obj_align(s_status_label, LV_ALIGN_TOP_MID, 0, 42);

    lv_obj_t *lat_label = lv_label_create(panel);
    lv_label_set_text(lat_label, "Latitude");
    lv_obj_align(lat_label, LV_ALIGN_TOP_LEFT, 12, 92);

    s_lat_textarea = lv_textarea_create(panel);
    lv_obj_set_size(s_lat_textarea, 230, 42);
    lv_obj_align(s_lat_textarea, LV_ALIGN_TOP_RIGHT, -12, 78);
    lv_textarea_set_one_line(s_lat_textarea, true);
    lv_textarea_set_accepted_chars(s_lat_textarea, "-0123456789.");
    lv_textarea_set_text(s_lat_textarea, kFallbackLatitude);
    lv_obj_add_event_cb(s_lat_textarea, textarea_event_cb, LV_EVENT_ALL, nullptr);

    lv_obj_t *lon_label = lv_label_create(panel);
    lv_label_set_text(lon_label, "Longitude");
    lv_obj_align(lon_label, LV_ALIGN_TOP_LEFT, 12, 148);

    s_lon_textarea = lv_textarea_create(panel);
    lv_obj_set_size(s_lon_textarea, 230, 42);
    lv_obj_align(s_lon_textarea, LV_ALIGN_TOP_RIGHT, -12, 134);
    lv_textarea_set_one_line(s_lon_textarea, true);
    lv_textarea_set_accepted_chars(s_lon_textarea, "-0123456789.");
    lv_textarea_set_text(s_lon_textarea, kFallbackLongitude);
    lv_obj_add_event_cb(s_lon_textarea, textarea_event_cb, LV_EVENT_ALL, nullptr);

    lv_obj_t *button = lv_button_create(panel);
    lv_obj_set_size(button, 180, 44);
    lv_obj_align(button, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_event_cb(button, manual_start_event_cb, LV_EVENT_CLICKED, nullptr);

    lv_obj_t *button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Open map");
    lv_obj_center(button_label);

    s_keyboard = lv_keyboard_create(s_startup_screen);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(s_keyboard, LV_PCT(100), 260);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

void gps_monitor_task(void *arg)
{
    (void)arg;
    gps_data_t gps_data = {};
    uint8_t displayed_satellites = UINT8_MAX;
    uint32_t processed_fix_ms = UINT32_MAX;
    bool displayed_fix_valid = false;

    while (true) {
        if (gps_get_data(&gps_data) == ESP_OK) {
            bool fix_valid = gps_data.valid && gps_get_time_since_update() <= kGpsStaleMs;
            bool new_fix = fix_valid && gps_data.last_update_ms != processed_fix_ms;
            bool status_changed = fix_valid != displayed_fix_valid ||
                                  gps_data.satellites_used != displayed_satellites;

            if ((new_fix || status_changed) && bsp_display_lock(1000)) {
                if (fix_valid && !s_map_started.load()) {
                    start_map(gps_data.latitude, gps_data.longitude);
                }

                if (s_map_started.load()) {
                    SimpleMap::update_gps_fix(gps_data.latitude, gps_data.longitude,
                                              gps_data.satellites_used, fix_valid);
                } else if (s_status_label) {
                    lv_label_set_text_fmt(s_status_label, "Waiting for LC76G fix... satellites: %u",
                                          gps_data.satellites_used);
                }
                bsp_display_unlock();

                if (fix_valid && (!displayed_fix_valid || gps_data.satellites_used != displayed_satellites)) {
                    ESP_LOGI(TAG, "LC76G fix: %.7f, %.7f; satellites=%u", gps_data.latitude,
                             gps_data.longitude, gps_data.satellites_used);
                }

                if (new_fix) {
                    processed_fix_ms = gps_data.last_update_ms;
                }
                displayed_fix_valid = fix_valid;
                displayed_satellites = gps_data.satellites_used;
            }
        }

        vTaskDelay(pdMS_TO_TICKS(kGpsPollMs));
    }
}

void attitude_monitor_task(void *arg)
{
    (void)arg;
    bno085_attitude_data_t attitude = {};

    while (true) {
        esp_err_t ret = bno085_attitude_get(&attitude);
        if (bsp_display_lock(1000)) {
            const bool valid = ret == ESP_OK && attitude.valid;
            if (s_map_started.load()) {
                SimpleMap::update_heading(attitude.heading_deg, valid);
                const uint32_t now = static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
                if (s_attitude_status_label && now >= s_attitude_message_until_ms &&
                    static_cast<uint32_t>(now - s_attitude_ui_last_update_ms) >= kAttitudeUiUpdateMs) {
                    char status[64] = {};
                    if (valid) {
                        if (attitude.accuracy_valid) {
                            snprintf(status, sizeof(status), "H %.0f  P %+.0f  R %+.0f  C%u",
                                     attitude.heading_deg, attitude.pitch_deg, attitude.roll_deg,
                                     attitude.accuracy <= 3 ? attitude.accuracy : 0);
                        } else {
                            snprintf(status, sizeof(status), "H %.0f  P %+.0f  R %+.0f",
                                     attitude.heading_deg, attitude.pitch_deg, attitude.roll_deg);
                        }
                    } else {
                        snprintf(status, sizeof(status), "IMU no data");
                    }
                    if (strcmp(lv_label_get_text(s_attitude_status_label), status) != 0) {
                        lv_label_set_text(s_attitude_status_label, status);
                    }
                    s_attitude_ui_last_update_ms = now;
                }
            }
            bsp_display_unlock();
        }

        vTaskDelay(pdMS_TO_TICKS(kAttitudePollMs));
    }
}

}  // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "Reset reason: %d", esp_reset_reason());

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    ret = mount_map_storage();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "No map storage available; map will report missing tiles");
    }

    bsp_display_cfg_t display_cfg = {
        .lvgl_port_cfg = {
            .task_priority = 4,
            .task_stack = 10 * 1024,
            .task_affinity = -1,
            .task_max_sleep_ms = 500,
            .timer_period_ms = 5,
        },
        .buffer_size = BSP_LCD_DRAW_BUFF_SIZE,
        .double_buffer = BSP_LCD_DRAW_BUFF_DOUBLE,
        .hw_cfg = {
            .hdmi_resolution = BSP_HDMI_RES_NONE,
            .dsi_bus = {
                .phy_clk_src = MIPI_DSI_PHY_CLK_SRC_DEFAULT,
                .lane_bit_rate_mbps = BSP_LCD_MIPI_DSI_LANE_BITRATE_MBPS,
            },
        },
        .flags = {
            .buff_dma = false,
            .buff_spiram = true,
            .sw_rotate = false,
        },
    };

    lv_display_t *display = bsp_display_start_with_config(&display_cfg);
    ESP_ERROR_CHECK(display ? ESP_OK : ESP_FAIL);
    ESP_ERROR_CHECK(bsp_display_backlight_on());

    ret = gps_init();
    s_gps_available = (ret == ESP_OK);
    if (!s_gps_available) {
        ESP_LOGE(TAG, "LC76G initialization failed: %s; manual coordinates remain available",
                 esp_err_to_name(ret));
    }

    ret = bno085_attitude_init();
    s_attitude_available = (ret == ESP_OK);
    if (!s_attitude_available) {
        ESP_LOGE(TAG, "BNO085 initialization failed: %s; map will continue without attitude",
                 esp_err_to_name(ret));
    }

    if (bsp_display_lock(1000)) {
        create_startup_screen();
        bsp_display_unlock();
    } else {
        ESP_LOGE(TAG, "Could not acquire LVGL lock for startup screen");
    }

    if (s_gps_available) {
        BaseType_t task_created = xTaskCreate(gps_monitor_task, "gps_monitor", 4096, nullptr, 8, nullptr);
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Could not create GPS monitor task");
        }
    }

    if (s_attitude_available) {
        BaseType_t task_created = xTaskCreate(attitude_monitor_task, "attitude_monitor", 4096, nullptr, 7, nullptr);
        if (task_created != pdPASS) {
            ESP_LOGE(TAG, "Could not create attitude monitor task");
        }
    }
}
