#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"
#include "esp_netif.h"

static const char *TAG = "WIFI_BLE_SCANNER";

// Global state variables
static bool event_loop_initialized = false;
static esp_netif_t *sta_netif = NULL;

/* ===================== RADIO CONTROL FUNCTIONS ===================== */
void stop_all_radio(void)
{
    ESP_LOGI(TAG, "Stopping all radio...");
    
    // Stop WiFi if running
    esp_wifi_stop();
    esp_wifi_deinit();
    
    // Clean up network interface
    if (sta_netif) {
        esp_netif_destroy(sta_netif);
        sta_netif = NULL;
    }
    
    // Note: Don't deinit event loop as it's used globally
    // Note: Don't release BT memory here as we're not using BT in this version
    
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_LOGI(TAG, "All radio stopped");
}

esp_err_t init_wifi_for_scan(void)
{
    ESP_LOGI(TAG, "Initializing WiFi for scanning...");
    
    // Make sure WiFi is stopped first
    esp_wifi_stop();
    esp_wifi_deinit();
    vTaskDelay(pdMS_TO_TICKS(100));
    
    // Initialize event loop only once
    if (!event_loop_initialized) {
        ESP_ERROR_CHECK(esp_netif_init());
        ESP_ERROR_CHECK(esp_event_loop_create_default());
        event_loop_initialized = true;
        ESP_LOGI(TAG, "Event loop initialized");
    }
    
    // Create network interface
    if (!sta_netif) {
        sta_netif = esp_netif_create_default_wifi_sta();
        assert(sta_netif);
        ESP_LOGI(TAG, "Network interface created");
    }
    
    // Initialize WiFi
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_start());
    
    vTaskDelay(pdMS_TO_TICKS(2000)); // Give WiFi time to initialize
    ESP_LOGI(TAG, "WiFi initialized for scanning");
    
    return ESP_OK;
}

esp_err_t perform_wifi_scan(char *result_buffer, size_t buffer_size)
{
    ESP_LOGI(TAG, "Performing WiFi scan...");
    
    wifi_scan_config_t scan_config = {
        .ssid = NULL,
        .bssid = NULL,
        .channel = 0,
        .show_hidden = true,
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
        .scan_time.active.min = 100,
        .scan_time.active.max = 300
    };
    
    esp_err_t ret = esp_wifi_scan_start(&scan_config, true);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Scan start failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    uint16_t ap_count = 0;
    ret = esp_wifi_scan_get_ap_num(&ap_count);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get AP count failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    if (ap_count == 0) {
        snprintf(result_buffer, buffer_size, "No networks found");
        ESP_LOGI(TAG, "No networks found");
        return ESP_OK;
    }
    
    // Limit to reasonable number
    uint16_t ap_num = (ap_count > 20) ? 20 : ap_count;
    wifi_ap_record_t ap_records[ap_num];
    memset(ap_records, 0, sizeof(ap_records));
    
    ret = esp_wifi_scan_get_ap_records(&ap_num, ap_records);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Get AP records failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // Format results
    char *ptr = result_buffer;
    int remaining = buffer_size;
    
    int written = snprintf(ptr, remaining, "Found %d networks:\n", ap_num);
    if (written > 0) {
        ptr += written;
        remaining -= written;
    }
    
    for (int i = 0; i < ap_num && remaining > 50; i++) {
        written = snprintf(ptr, remaining, "%2d: %-32s (%3d dBm) Ch:%2d\n", 
                          i+1, ap_records[i].ssid, ap_records[i].rssi, ap_records[i].primary);
        if (written > 0) {
            ptr += written;
            remaining -= written;
        }
    }
    
    ESP_LOGI(TAG, "Scan complete");
    return ESP_OK;
}

/* ===================== MAIN TASK ===================== */
void scanner_task(void *arg)
{
    char scan_results[512];
    
    while (1) {
        ESP_LOGI(TAG, "=== Starting scan cycle ===");
        
        // Step 1: Initialize and scan WiFi
        esp_err_t ret = init_wifi_for_scan();
        if (ret == ESP_OK) {
            memset(scan_results, 0, sizeof(scan_results));
            ret = perform_wifi_scan(scan_results, sizeof(scan_results));
        }
        
        // Step 2: Stop WiFi after scan (but keep event loop)
        esp_wifi_stop();
        esp_wifi_deinit();
        vTaskDelay(pdMS_TO_TICKS(100));
        
        // Step 3: Process and display results
        if (strlen(scan_results) > 0) {
            ESP_LOGI(TAG, "Scan Results:\n%s", scan_results);
            
            // Here you would normally send via BLE
            // For now, just print to serial
        }
        
        // Wait before next scan
        ESP_LOGI(TAG, "Waiting 30 seconds before next scan...\n");
        vTaskDelay(pdMS_TO_TICKS(30000));
    }
}

/* ===================== MAIN ===================== */
void app_main(void)
{
    ESP_LOGI(TAG, "=== ESP32-C3 WiFi Scanner ===");
    
    // Initialize NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);
    
    // Start scanning task
    xTaskCreate(scanner_task, "scanner", 4096, NULL, 5, NULL);
    
    ESP_LOGI(TAG, "System started. Scanning WiFi every 30 seconds...");
    
    // Keep main task alive
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10000));
    }
}