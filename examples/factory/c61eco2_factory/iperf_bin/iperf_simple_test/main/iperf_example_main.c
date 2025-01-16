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

#define BLINK_GPIO GPIO_NUM_8
#define BUTTON_PIN GPIO_NUM_9

#define LED_STRIP_RMT_RES_HZ  (10 * 1000 * 1000)
static uint8_t s_led_state = 0;
static led_strip_handle_t led_strip;

static void configure_led(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BLINK_GPIO,
        .max_leds = 1,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };

    // LED strip backend configuration: SPI
    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT, // different clock source can lead to different power consumption
        .flags.with_dma = true,         // Using DMA can improve performance and help drive more LEDs
        .spi_bus = SPI2_HOST,           // SPI bus ID
    };

    ESP_ERROR_CHECK(led_strip_new_spi_device(&strip_config, &spi_config, &led_strip));

     ESP_LOGI(TAG, "configure led strip success!");

    ESP_ERROR_CHECK(led_strip_clear(led_strip));
}

void set_led_color(uint8_t state)
{
    switch (state) {
        case 1:
            led_strip_set_pixel(led_strip, 0, 20, 20, 0); // yellow
            break;
        case 2:
            led_strip_set_pixel(led_strip, 0, 20, 0, 20); // Purple
            break;
        case 3:
            led_strip_set_pixel(led_strip, 0, 0, 20, 20); // Green
            break;
        case 4:
            led_strip_set_pixel(led_strip, 0, 15, 15, 15); // white
            break;
        default:
            led_strip_clear(led_strip); // Off
            return;
    }
    led_strip_refresh(led_strip);
}

static void button_single_click_cb(void *arg, void *usr_data)
{
    ESP_ERROR_CHECK(!(BUTTON_SINGLE_CLICK == iot_button_get_event(arg)));

    s_led_state = (s_led_state + 1) % 5;
    set_led_color(s_led_state);
    ESP_LOGI(TAG, "LED State: %d", s_led_state);
}

static void button_long_press_cb(void *arg, void *usr_data)
{
    // esp_wifi_set_band(WIFI_BAND_2G);
    ESP_ERROR_CHECK(!(BUTTON_LONG_PRESS_START == iot_button_get_event(arg)));
    ESP_LOGI(TAG, "Starting full-channel scan...");
    ESP_LOGI(TAG, "It will take few more seconds to complete the scan, please wait");

    const char *argv[] = {"sta_scan", "--show-hidden", "--passive"};
    int argc = sizeof(argv) / sizeof(argv[0]);

    int ret = cmd_do_sta_scan(argc, (char **)argv);
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

    iot_button_register_cb(button_handle, BUTTON_SINGLE_CLICK, button_single_click_cb, NULL);
    iot_button_register_cb(button_handle, BUTTON_LONG_PRESS_START, button_long_press_cb, NULL);
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

	configure_led();
    button_init();

    printf("\n ==========================================================\n");
    printf(" |              ESP32-C5 board simple test                |\n");
    printf(" |                                                        |\n");
    printf(" |  1. Press BOOT button to check LED                     |\n");
    printf(" |  2. Long press BOOT button to start full channel scan  |\n");
    printf(" |                                                        |\n");
    printf(" ==========================================================\n\n");

}
