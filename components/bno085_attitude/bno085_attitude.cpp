#include "bno085_attitude.h"

#include <atomic>
#include <cmath>

#include "bno085_rvc_parser.hpp"
#include "driver/uart.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace {

constexpr uart_port_t kUartPort = UART_NUM_2;
constexpr int kRxGpio = 33;
constexpr int kBaudRate = 115200;
constexpr int kRxBufferSize = 2048;
constexpr uint32_t kFirstFrameTimeoutMs = 1500;
constexpr uint32_t kStaleMs = 500;
constexpr EventBits_t kFirstFrameBit = BIT0;

const char *TAG = "bno085_attitude";
SemaphoreHandle_t s_data_mutex = nullptr;
EventGroupHandle_t s_events = nullptr;
TaskHandle_t s_rx_task = nullptr;
bno085_attitude_data_t s_data = {};
float s_raw_yaw_deg = 0.0f;
float s_yaw_offset_deg = 0.0f;
bool s_uart_installed = false;
std::atomic<bool> s_first_frame_signaled{false};

float normalize_heading(float heading)
{
    heading = fmodf(heading, 360.0f);
    return heading < 0.0f ? heading + 360.0f : heading;
}

uint32_t now_ms()
{
    return static_cast<uint32_t>(esp_timer_get_time() / 1000ULL);
}

void publish_sample(const bno085_rvc_sample_t &sample)
{
    if (!std::isfinite(sample.yaw_deg) || !std::isfinite(sample.pitch_deg) ||
        !std::isfinite(sample.roll_deg)) {
        return;
    }
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return;
    }

    s_raw_yaw_deg = normalize_heading(sample.yaw_deg);
    s_data.heading_deg = normalize_heading(s_raw_yaw_deg - s_yaw_offset_deg);
    s_data.pitch_deg = sample.pitch_deg;
    s_data.roll_deg = sample.roll_deg;
    s_data.accuracy = 0;
    s_data.accuracy_valid = false;
    s_data.valid = true;
    s_data.last_update_ms = now_ms();
    xSemaphoreGive(s_data_mutex);
    bool expected = false;
    if (s_first_frame_signaled.compare_exchange_strong(expected, true)) {
        xEventGroupSetBits(s_events, kFirstFrameBit);
    }
}

void rvc_rx_task(void *)
{
    Bno085RvcParser parser;
    uint8_t buffer[128];
    uint32_t last_error_log_ms = 0;
    uint32_t reported_rejected_frames = 0;

    while (true) {
        const int length = uart_read_bytes(kUartPort, buffer, sizeof(buffer), pdMS_TO_TICKS(50));
        if (length < 0) {
            const uint32_t now = now_ms();
            if (static_cast<uint32_t>(now - last_error_log_ms) >= 1000) {
                ESP_LOGW(TAG, "UART-RVC read failed");
                last_error_log_ms = now;
            }
            continue;
        }

        for (int i = 0; i < length; ++i) {
            bno085_rvc_sample_t sample = {};
            if (parser.push(buffer[i], &sample)) {
                publish_sample(sample);
            }
        }
        if (parser.rejected_frames() != reported_rejected_frames) {
            const uint32_t now = now_ms();
            if (static_cast<uint32_t>(now - last_error_log_ms) >= 1000) {
                ESP_LOGW(TAG, "Rejected %lu invalid UART-RVC frame(s)",
                         static_cast<unsigned long>(parser.rejected_frames() -
                                                    reported_rejected_frames));
                reported_rejected_frames = parser.rejected_frames();
                last_error_log_ms = now;
            }
        }
    }

}

void cleanup()
{
    if (s_rx_task) {
        vTaskDelete(s_rx_task);
    }
    s_rx_task = nullptr;
    if (s_uart_installed) {
        uart_driver_delete(kUartPort);
        s_uart_installed = false;
    }
    if (s_events) {
        vEventGroupDelete(s_events);
        s_events = nullptr;
    }
    if (s_data_mutex) {
        vSemaphoreDelete(s_data_mutex);
        s_data_mutex = nullptr;
    }
    s_data = {};
    s_raw_yaw_deg = 0.0f;
    s_yaw_offset_deg = 0.0f;
    s_first_frame_signaled.store(false);
}

}  // namespace

extern "C" esp_err_t bno085_attitude_init(void)
{
    if (s_data_mutex || s_rx_task) {
        return ESP_ERR_INVALID_STATE;
    }

    s_data_mutex = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    if (!s_data_mutex || !s_events) {
        cleanup();
        return ESP_ERR_NO_MEM;
    }

    const uart_config_t uart_config = {
        .baud_rate = kBaudRate,
        .data_bits = UART_DATA_8_BITS,
        .parity = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .rx_flow_ctrl_thresh = 0,
        .source_clk = UART_SCLK_DEFAULT,
        .flags = {},
    };

    esp_err_t ret = uart_driver_install(kUartPort, kRxBufferSize, 0, 0, nullptr, 0);
    if (ret == ESP_OK) {
        s_uart_installed = true;
        ret = uart_param_config(kUartPort, &uart_config);
    }
    if (ret == ESP_OK) {
        ret = uart_set_pin(kUartPort, UART_PIN_NO_CHANGE, kRxGpio,
                           UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    }
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Could not configure UART-RVC: %s", esp_err_to_name(ret));
        cleanup();
        return ret;
    }

    uart_flush_input(kUartPort);
    s_first_frame_signaled.store(false);
    if (xTaskCreate(rvc_rx_task, "bno085_rvc_rx", 4096, nullptr, 6, &s_rx_task) != pdPASS) {
        cleanup();
        return ESP_ERR_NO_MEM;
    }

    const EventBits_t bits = xEventGroupWaitBits(s_events, kFirstFrameBit, pdFALSE, pdTRUE,
                                                 pdMS_TO_TICKS(kFirstFrameTimeoutMs));
    if ((bits & kFirstFrameBit) == 0) {
        ESP_LOGE(TAG, "No valid UART-RVC frame received within %u ms",
                 static_cast<unsigned>(kFirstFrameTimeoutMs));
        cleanup();
        return ESP_ERR_TIMEOUT;
    }

    ESP_LOGI(TAG, "BNO085 ready: UART%d RX=GPIO%d, %d baud, RVC mode",
             static_cast<int>(kUartPort), kRxGpio, kBaudRate);
    return ESP_OK;
}

extern "C" esp_err_t bno085_attitude_get(bno085_attitude_data_t *data)
{
    if (!data) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_data_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(20)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }

    *data = s_data;
    if (data->valid && static_cast<uint32_t>(now_ms() - data->last_update_ms) > kStaleMs) {
        data->valid = false;
    }
    xSemaphoreGive(s_data_mutex);
    return ESP_OK;
}

extern "C" esp_err_t bno085_attitude_set_heading_zero(void)
{
    if (!s_data_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (xSemaphoreTake(s_data_mutex, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    if (!s_data.valid || static_cast<uint32_t>(now_ms() - s_data.last_update_ms) > kStaleMs) {
        xSemaphoreGive(s_data_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    s_yaw_offset_deg = s_raw_yaw_deg;
    s_data.heading_deg = 0.0f;
    xSemaphoreGive(s_data_mutex);
    ESP_LOGI(TAG, "Heading zero set for this power session");
    return ESP_OK;
}
