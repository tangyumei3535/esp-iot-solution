/* Wi-Fi iperf Example

   This example code is in the Public Domain (or CC0 licensed, at your option.)

   Unless required by applicable law or agreed to in writing, this
   software is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
   CONDITIONS OF ANY KIND, either express or implied.
*/

#include <errno.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_log.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_console.h"
#include "cmd_system.h"

#include "esp32c61/rom/rtc.h"

/* component manager */
#include "iperf.h"
#include "wifi_cmd.h"
#include "iperf_cmd.h"
#include "ping_cmd.h"

int cmd_do_sta_scan(int argc, char **argv);

#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS || CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
#include "esp_wifi_he.h"
#endif
#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
extern int wifi_cmd_get_tx_statistics(int argc, char **argv);
extern int wifi_cmd_clr_tx_statistics(int argc, char **argv);
#endif
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
extern int wifi_cmd_get_rx_statistics(int argc, char **argv);
extern int wifi_cmd_clr_rx_statistics(int argc, char **argv);
#endif

#ifdef CONFIG_ESP_EXT_CONN_ENABLE
#include "esp_extconn.h"
#endif

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "led_strip.h"
#include "sdkconfig.h"
#include "esp_rom_gpio.h"
#include "iot_button.h"

#include "argtable3/argtable3.h"

static const char *TAG = "ESP32-C61_Factory";

#define BUTTON_PIN GPIO_NUM_9

#define RST_REASON_BIT  0x80000000
#define RST_REASON_MASK 0x7FFF
#define RST_REASON_SHIFT 16

static void button_press_repeat_cb(void *arg, void *usr_data)
{
    button_handle_t button_handle = (button_handle_t)arg;
    uint8_t repeat_count = iot_button_get_repeat(button_handle);
    if (repeat_count == 5) {
        ESP_LOGI(TAG, "Five clicks detected, restarting to Simple test bin ...");

        uint32_t hint = 0xAA; // 自定义复位原因值
        assert((hint & (~RST_REASON_MASK)) == 0);
        uint32_t val = hint | (hint << RST_REASON_SHIFT) | RST_REASON_BIT;
        REG_WRITE(RTC_RESET_CAUSE_REG, val); // 写入复位原因
        esp_restart();
    }
}

static void button_init(void)
{
    button_config_t button_config = {
        .type = BUTTON_TYPE_GPIO,
        .gpio_button_config = {
            .gpio_num = BUTTON_PIN,
            .active_level = 0,
        },
    };

    button_handle_t button_handle = iot_button_create(&button_config);

    iot_button_register_cb(button_handle, BUTTON_PRESS_REPEAT, button_press_repeat_cb, NULL);
}

void iperf_hook_show_wifi_stats(iperf_traffic_type_t type, iperf_status_t status)
{
    if (status == IPERF_STARTED) {
#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
        if (type != IPERF_UDP_SERVER) {
            wifi_cmd_clr_tx_statistics(0, NULL);
        }
#endif
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
        if (type != IPERF_UDP_CLIENT) {
            wifi_cmd_clr_rx_statistics(0, NULL);
        }
#endif
    }

    if (status == IPERF_STOPPED) {
#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
        if (type != IPERF_UDP_SERVER) {
            wifi_cmd_get_tx_statistics(0, NULL);
        }
#endif
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
        if (type != IPERF_UDP_CLIENT) {
            wifi_cmd_get_rx_statistics(0, NULL);
        }
#endif
    }

}

void app_main(void)
{
    printf("Iperf example started\n");
#if CONFIG_ESP_EXT_CONN_ENABLE
    esp_extconn_config_t ext_config = ESP_EXTCONN_CONFIG_DEFAULT();
    esp_extconn_init(&ext_config);
#endif

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK( ret );

    /* initialise wifi */
    app_wifi_initialise_config_t config = APP_WIFI_CONFIG_DEFAULT();
    config.storage = WIFI_STORAGE_RAM;
    config.ps_type = WIFI_PS_NONE;
    app_initialise_wifi(&config);
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_STATS
#if CONFIG_ESP_WIFI_ENABLE_WIFI_RX_MU_STATS
    esp_wifi_enable_rx_statistics(true, true);
#else
    esp_wifi_enable_rx_statistics(true, false);
#endif
#endif
#if CONFIG_ESP_WIFI_ENABLE_WIFI_TX_STATS
    esp_wifi_enable_tx_statistics(ESP_WIFI_ACI_BE, true);
#endif


    esp_console_repl_t *repl = NULL;
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.prompt = "iperf>";

    // init console REPL environment
#if CONFIG_ESP_CONSOLE_UART
    esp_console_dev_uart_config_t uart_config = ESP_CONSOLE_DEV_UART_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_uart(&uart_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_CDC
    esp_console_dev_usb_cdc_config_t cdc_config = ESP_CONSOLE_DEV_CDC_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_cdc(&cdc_config, &repl_config, &repl));
#elif CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
    esp_console_dev_usb_serial_jtag_config_t usbjtag_config = ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_console_new_repl_usb_serial_jtag(&usbjtag_config, &repl_config, &repl));
#endif

    /* Register commands */
    register_system();
    app_register_all_wifi_commands();
    app_register_iperf_commands();
    ping_cmd_register_ping();
    app_register_iperf_hook_func(iperf_hook_show_wifi_stats);

    button_init();


    printf("\n ==================================================\n");
    printf(" |       Steps to test WiFi throughput            |\n");
    printf(" |                                                |\n");
    printf(" |  1. Print 'help' to gain overview of commands  |\n");
    printf(" |  2. Configure device to station or soft-AP     |\n");
    printf(" |  3. Setup WiFi connection                      |\n");
    printf(" |  4. Run iperf to test UDP/TCP RX/TX throughput |\n");
    printf(" |                                                |\n");
    printf(" =================================================\n\n");

    // start console REPL
    ESP_ERROR_CHECK(esp_console_start_repl(repl));
}
