#include <stdio.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "esp_log.h"
#include "nvs_flash.h"
#include "esp_wifi.h"
#include "esp_event.h"

#include "nimble/nimble_port.h"
#include "nimble/nimble_port_freertos.h"
#include "host/ble_hs.h"
#include "services/gap/ble_svc_gap.h"
#include "services/gatt/ble_svc_gatt.h"

static const char *TAG = "BLE_WIFI";

static uint16_t conn_handle = 0;
static uint16_t notify_handle;

#define DEVICE_NAME "ESP32C3_WIFI"
#define WIFI_SERVICE_UUID     0x180F
#define WIFI_CHAR_UUID        0x2A19

/* ===================== GATT ACCESS ===================== */
static int gatt_access_cb(uint16_t conn_handle, uint16_t attr_handle,
                          struct ble_gatt_access_ctxt *ctxt, void *arg)
{
    return 0;
}

/* ===================== GATT SERVER ===================== */
static const struct ble_gatt_svc_def gatt_svcs[] = {
    {
        .type = BLE_GATT_SVC_TYPE_PRIMARY,
        .uuid = BLE_UUID16_DECLARE(WIFI_SERVICE_UUID),
        .characteristics = (struct ble_gatt_chr_def[]) {
            {
                .uuid = BLE_UUID16_DECLARE(WIFI_CHAR_UUID),
                .access_cb = gatt_access_cb,
                .flags = BLE_GATT_CHR_F_NOTIFY,
                .val_handle = &notify_handle,
            },
            {0}
        }
    },
    {0}
};

/* ===================== BLE SYNC ===================== */
static void ble_app_on_sync(void)
{
    ble_svc_gap_device_name_set(DEVICE_NAME);
    ble_gatts_add_svcs(gatt_svcs);

    struct ble_gap_adv_params adv_params = {0};
    adv_params.conn_mode = BLE_GAP_CONN_MODE_UND;
    adv_params.disc_mode = BLE_GAP_DISC_MODE_GEN;

    ble_gap_adv_start(BLE_OWN_ADDR_PUBLIC, NULL, BLE_HS_FOREVER,
                      &adv_params, NULL, NULL);

    ESP_LOGI(TAG, "BLE Advertising");
}

/* ===================== BLE INIT ===================== */
void ble_init(void)
{
    nimble_port_init();
    ble_svc_gap_init();
    ble_svc_gatt_init();

    ble_hs_cfg.sync_cb = ble_app_on_sync;

    nimble_port_freertos_init(NULL);
    ESP_LOGI(TAG, "NimBLE Initialized");
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

/* ===================== WIFI SCAN TASK ===================== */
void wifi_scan_task(void *arg)
{
    wifi_ap_record_t ap[20];
    uint16_t ap_num;
    char msg[100];

    while (1) {
        esp_wifi_scan_start(NULL, true);
        esp_wifi_scan_get_ap_num(&ap_num);
        esp_wifi_scan_get_ap_records(&ap_num, ap);

        for (int i = 0; i < ap_num; i++) {
            snprintf(msg, sizeof(msg), "%s | RSSI: %d\n",
                     ap[i].ssid, ap[i].rssi);

            struct os_mbuf *om = ble_hs_mbuf_from_flat(msg, strlen(msg));
            ble_gatts_notify_custom(conn_handle, notify_handle, om);
        }

        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

/* ===================== MAIN ===================== */
void app_main(void)
{
    nvs_flash_init();
    wifi_init();
    ble_init();

    xTaskCreate(wifi_scan_task, "wifi_scan", 4096, NULL, 5, NULL);
}
