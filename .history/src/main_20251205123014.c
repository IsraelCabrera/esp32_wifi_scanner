#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "esp_bt.h"
#include "esp_gap_ble_api.h"
#include "esp_gatts_api.h"
#include "esp_bt_main.h"
#include "esp_gatt_common_api.h"

#define DEVICE_NAME "ESP32C3_WIFI_SCANNER"
#define SERVICE_UUID 0x00FF
#define CHAR_UUID    0xFF01

static const char *TAG = "BLE_WIFI";

static uint16_t service_handle;
static esp_gatt_if_t gatts_if_global;
static uint16_t conn_id_global;
static bool device_connected = false;

/* ===================== BLE GATT CALLBACK ===================== */
static void gatts_event_handler(esp_gatts_cb_event_t event,
                                esp_gatt_if_t gatts_if,
                                esp_ble_gatts_cb_param_t *param)
{
    switch (event) {

    case ESP_GATTS_REG_EVT:
        esp_ble_gap_set_device_name(DEVICE_NAME);

        esp_gatt_srvc_id_t service_id = {
            .is_primary = true,
            .id.inst_id = 0,
            .id.uuid.len = ESP_UUID_LEN_16,
            .id.uuid.uuid.uuid16 = SERVICE_UUID,
        };

        esp_ble_gatts_create_service(gatts_if, &service_id, 4);
        break;

    case ESP_GATTS_CREATE_EVT:
        service_handle = param->create.service_handle;

        esp_gatt_char_prop_t property = ESP_GATT_CHAR_PROP_BIT_NOTIFY |
                                        ESP_GATT_CHAR_PROP_BIT_READ;

        esp_attr_value_t char_value = {
            .attr_max_len = 200,
            .attr_len = 0,
            .attr_value = NULL,
        };

        esp_bt_uuid_t char_uuid = {
            .len = ESP_UUID_LEN_16,
            .uuid.uuid16 = CHAR_UUID
        };

        esp_ble_gatts_add_char(service_handle, &char_uuid,
            ESP_GATT_PERM_READ, property, &char_value, NULL);

        esp_ble_gatts_start_service(service_handle);
        break;

    case ESP_GATTS_CONNECT_EVT:
        gatts_if_global = gatts_if;
        conn_id_global = param->connect.conn_id;
        device_connected = true;
        ESP_LOGI(TAG, "BLE Connected");
        break;

    case ESP_GATTS_DISCONNECT_EVT:
        device_connected = false;
        esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
            .adv_int_min = 0x20,
            .adv_int_max = 0x40,
            .adv_type = ADV_TYPE_IND,
            .channel_map = ADV_CHNL_ALL,
            .own_addr_type = BLE_ADDR_TYPE_PUBLIC
        });
        ESP_LOGI(TAG, "BLE Disconnected");
        break;

    default:
        break;
    }
}

/* ===================== BLE INIT ===================== */
void ble_init(void)
{
    esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);

    esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
    esp_bt_controller_init(&bt_cfg);
    esp_bt_controller_enable(ESP_BT_MODE_BLE);

    esp_bluedroid_init();
    esp_bluedroid_enable();

    esp_ble_gatts_register_callback(gatts_event_handler);
    esp_ble_gatts_app_register(0);

    esp_ble_gap_start_advertising(&(esp_ble_adv_params_t){
        .adv_int_min = 0x20,
        .adv_int_max = 0x40,
        .adv_type = ADV_TYPE_IND,
        .channel_map = ADV_CHNL_ALL,
        .own_addr_type = BLE_ADDR_TYPE_PUBLIC
    });

    ESP_LOGI(TAG, "BLE Initialized");
}

/* ===================== WIFI SCAN ===================== */
void wifi_scan_task(void *arg)
{
    uint16_t ap_num = 0;
    wifi_ap_record_t ap_records[20];
    char msg[200];

    while (1) {
        esp_wifi_scan_start(NULL, true);
        esp_wifi_scan_get_ap_num(&ap_num);
        esp_wifi_scan_get_ap_records(&ap_num, ap_records);

        for (int i = 0; i < ap_num && device_connected; i++) {
            snprintf(msg, sizeof(msg),
                     "SSID: %s | RSSI: %d dBm\n",
                     ap_records[i].ssid,
                     ap_records[i].rssi);

            esp_ble_gatts_send_indicate(
                gatts_if_global,
                conn_id_global,
                0,
                strlen(msg),
                (uint8_t *)msg,
                false
            );
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ===================== WIFI INIT ===================== */
void wifi_init(void)
{
    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);
    esp_wifi_start();
}

/* ===================== MAIN ===================== */
void app_main(void)
{
    nvs_flash_init();
    wifi_init();
    ble_init();

    xTaskCreate(wifi_scan_task, "wifi_scan", 4096, NULL, 5, NULL);
}
