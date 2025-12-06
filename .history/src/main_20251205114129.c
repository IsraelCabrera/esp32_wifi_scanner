#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_log.h"
#include "nvs_flash.h"

#define CONFIG_CLASSIC_BT_ENABLED 1
#define CONFIG_BT_SPP_ENABLED 1
// Bluetooth includes - updated for ESP-IDF 5.x
#include "esp_bt.h"
#include "esp_gap_bt_api.h"
#include "esp_spp_api.h"
#include "esp_bt_device.h"
#include "esp_bt_main.h"

static const char *TAG = "WIFI_BT_SCANNER";

// Bluetooth variables
static bool bt_connected = false;
static uint32_t spp_handle = 0;

// WiFi scan variables
#define MAX_APS 20
static wifi_ap_record_t ap_info[MAX_APS];
static uint16_t ap_count = 0;

// SPP callback function
static void spp_callback(esp_spp_cb_event_t event, esp_spp_cb_param_t *param)
{
    switch (event) {
        case ESP_SPP_INIT_EVT:
            ESP_LOGI(TAG, "SPP initialized");
            esp_spp_start_srv(ESP_SPP_SEC_NONE, ESP_SPP_ROLE_SLAVE, 0, "ESP32-WIFI-Scanner");
            break;
            
        case ESP_SPP_START_EVT:
            ESP_LOGI(TAG, "SPP server started");
            // Set Bluetooth device name
            esp_bt_gap_set_device_name("ESP32-WIFI-Scanner");
            // Make device discoverable
            esp_bt_gap_set_scan_mode(ESP_BT_CONNECTABLE, ESP_BT_GENERAL_DISCOVERABLE);
            break;
            
        case ESP_SPP_SRV_OPEN_EVT:
            ESP_LOGI(TAG, "Client connected");
            bt_connected = true;
            spp_handle = param->open.handle;
            break;
            
        case ESP_SPP_CLOSE_EVT:
            ESP_LOGI(TAG, "Client disconnected");
            bt_connected = false;
            spp_handle = 0;
            break;
            
        case ESP_SPP_DATA_IND_EVT:
            if (param->data_ind.len > 0) {
                char cmd[10];
                int len = param->data_ind.len < sizeof(cmd) ? param->data_ind.len : sizeof(cmd) - 1;
                memcpy(cmd, param->data_ind.data, len);
                cmd[len] = '\0';
                
                if (strcmp(cmd, "SCAN") == 0) {
                    ESP_LOGI(TAG, "Scan requested via Bluetooth");
                }
            }
            break;
            
        default:
            break;
    }
}

// Initialize Bluetooth
static void init_bluetooth(void)
{
    esp_err_t ret;
    
    // Initialize Bluetooth controller with default config
    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    ret = esp_bt_controller_init(&bt_cfg);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_bt_controller_enable(ESP_BT_MODE_CLASSIC_BT);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluetooth controller enable failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Initialize Bluedroid
    ret = esp_bluedroid_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_bluedroid_enable();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(ret));
        return;
    }
    
    // Register SPP callback and initialize SPP
    ret = esp_spp_register_callback(spp_callback);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPP callback register failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ret = esp_spp_init(ESP_SPP_MODE_CB);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPP init failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ESP_LOGI(TAG, "Bluetooth initialized successfully");
}

// Initialize WiFi
static void init_wifi(void)
{
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    ESP_LOGI(TAG, "WiFi initialized");
}

// Scan for WiFi networks
static void scan_wifi_networks(void)
{
    wifi_scan_config_t scan_conf = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time = { .active = { .min = 100, .max = 300 } }
    };
    
    ESP_LOGI(TAG, "Starting WiFi scan...");
    
    esp_err_t ret = esp_wifi_scan_start(&scan_conf, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "WiFi scan failed: %s", esp_err_to_name(ret));
        return;
    }
    
    ap_count = MAX_APS;
    ret = esp_wifi_scan_get_ap_records(&ap_count, ap_info);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to get scan results: %s", esp_err_to_name(ret));
        ap_count = 0;
        return;
    }
    
    ESP_LOGI(TAG, "Found %d WiFi networks", ap_count);
}

// Send scan results via Bluetooth
static void send_scan_results(void)
{
    if (!bt_connected || spp_handle == 0 || ap_count == 0) {
        return;
    }
    
    char buffer[512];
    char *ptr = buffer;
    int remaining = sizeof(buffer);
    int written;
    
    // Send start marker
    written = snprintf(ptr, remaining, "=== WiFi Scan Results ===\n");
    ptr += written;
    remaining -= written;
    
    // Send each network
    for (int i = 0; i < ap_count && remaining > 50; i++) {
        char ssid_str[33] = {0};
        memcpy(ssid_str, ap_info[i].ssid, 32);
        
        written = snprintf(ptr, remaining, "SSID: %s\n", ssid_str);
        ptr += written;
        remaining -= written;
        
        written = snprintf(ptr, remaining, "  RSSI: %d dBm\n", ap_info[i].rssi);
        ptr += written;
        remaining -= written;
        
        written = snprintf(ptr, remaining, "  Channel: %d\n", ap_info[i].primary);
        ptr += written;
        remaining -= written;
        
        written = snprintf(ptr, remaining, "  Auth: %d\n\n", ap_info[i].authmode);
        ptr += written;
        remaining -= written;
    }
    
    // Send end marker
    written = snprintf(ptr, remaining, "=== End of Results ===\n");
    ptr += written;
    
    // Send data via SPP
    if (esp_spp_write(spp_handle, strlen(buffer), (uint8_t *)buffer) == ESP_OK) {
        ESP_LOGI(TAG, "Sent scan results via Bluetooth");
    } else {
        ESP_LOGE(TAG, "Failed to send via Bluetooth");
    }
}

// Main scanning task
static void scanning_task(void *arg)
{
    while (1) {
        if (bt_connected) {
            scan_wifi_networks();
            send_scan_results();
        }
        vTaskDelay(5000 / portTICK_PERIOD_MS); // Scan every 5 seconds when connected
    }
}

// Main application entry point
void app_main(void)
{
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    ESP_LOGI(TAG, "Starting WiFi/Bluetooth Scanner...");
    
    // Initialize WiFi and Bluetooth
    init_wifi();
    init_bluetooth();
    
    // Create scanning task
    xTaskCreate(scanning_task, "scanning_task", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "Setup complete. Device name: ESP32-WIFI-Scanner");
    ESP_LOGI(TAG, "Connect via Bluetooth SPP to receive WiFi scan data");
    
    // Keep main task alive
    while (1) {
        vTaskDelay(1000 / portTICK_PERIOD_MS);
    }
}
