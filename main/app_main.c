#include <stdio.h>
#include "esp_system.h"
#include "nvs_flash.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "protocol_examples_common.h"
#include "esp_log.h"
#include "mqtt_client.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/adc.h"
#include "driver/gpio.h"
#include "esp_log.h"


static const char *SENSOR = "HD38_SENSOR";
static const char *TAG = "mqtt5_wifi";


// ----------------------
// Pin Configuration
// ----------------------
#define AO_GPIO         3
#define AO_CHANNEL      ADC1_CHANNEL_3   // GPIO3 → ADC1 Channel 3
#define ADC_ATTEN       ADC_ATTEN_DB_11  // Allows ~0–3.3V range
#define ADC_WIDTH       ADC_WIDTH_BIT_12 // 12-bit ADC (0–4095)
#define RELAY_GPIO      9

// ----------------------
// Calibration Constants
// ----------------------
#define DRY_VOLTAGE     3.3f   // Voltage when sensor is dry (in air)
#define WET_VOLTAGE     0.3f   // Voltage when sensor is in water
#define TIME_TO_STOP  10000    // Time the motor stays on in ms (10s rn)


static void mqtt_event_handler(void *handler_args, esp_event_base_t base, int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;
    esp_mqtt_client_handle_t client = event->client;

    switch ((esp_mqtt_event_id_t)event_id) {
    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT connected");
        esp_mqtt_client_publish(client, "/topic/test", "Hello from ESP32", 0, 1, 0);
        break;
    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT disconnected");
        break;
    case MQTT_EVENT_PUBLISHED:
        ESP_LOGI(TAG, "Message published, msg_id=%d", event->msg_id);
        break;
    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
        break;
    default:
        break;
    }
}

esp_mqtt_client_handle_t setup_mqtt(void)
{
    ESP_LOGI(TAG, "Starting minimal MQTT v5 example over Wi-Fi");

    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    ESP_ERROR_CHECK(example_connect());  // Connect to Wi-Fi

    esp_mqtt_client_config_t mqtt_cfg = {
        .broker.address.uri = "mqtt://172.21.64.93",
        .session.protocol_ver = MQTT_PROTOCOL_V_5,
        .credentials.username = "mqttuser",
        .credentials.authentication.password = "mq6850tt",
        .network.disable_auto_reconnect = false,
    };

    esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(client);
    return client;
}


static float adc_to_voltage(int raw)
{
    return (float)raw * 3.3f / 4095.0f;
}

static float voltage_to_moisture(float voltage)
{
    float pct = ((DRY_VOLTAGE - voltage) / (DRY_VOLTAGE - WET_VOLTAGE)) * 100.0f;
    if (pct < 0) pct = 0;
    if (pct > 100) pct = 100;
    return pct;
}
void realse_motor(esp_mqtt_client_handle_t client) {
    // Log the message
    ESP_LOGI(SENSOR, "Starting the motor");
    esp_mqtt_client_publish(client, "/topic/test", "Starting the motor", 0, 1, 0);
    // Start the motor
    gpio_set_level(RELAY_GPIO, 1);
    // Give water time to flow 
    vTaskDelay(pdMS_TO_TICKS(TIME_TO_STOP)); // wait 10 seconds
    // Stop the motor
    gpio_set_level(RELAY_GPIO, 0);
    ESP_LOGI(SENSOR, "Stopping the motor");
    esp_mqtt_client_publish(client, "/topic/test", "Stopping the motor", 0, 1, 0);

}

// ---------------------->
// Humidity Function
// ----------------------
static void humidity(esp_mqtt_client_handle_t client)
{
    int raw = adc1_get_raw(AO_CHANNEL);
    float voltage = adc_to_voltage(raw);
    float moisture = voltage_to_moisture(voltage);

    if (voltage >= DRY_VOLTAGE) {
        realse_motor(client);

    }

    char msg[128];  // Make sure the buffer is large enough for your message

    // Format the message
    snprintf(msg, sizeof(msg), "AO raw=%d | Voltage=%.2f V | Moisture=%.1f%%", raw, voltage, moisture);

    // Log the message
    ESP_LOGI(SENSOR, "%s", msg);

    // Publish the message via MQTT
    esp_mqtt_client_publish(client, "/topic/test", msg, 0, 1, 0);
}

// ----------------------
// Main Application
// ----------------------
void app_main(void)
{
    ESP_LOGI(TAG, "Starting HD-38 humidity + motor test");

    //set up mqtt conttection 
    esp_mqtt_client_handle_t client = setup_mqtt();

    // Configure ADC
    adc1_config_width(ADC_WIDTH);
    adc1_config_channel_atten(AO_CHANNEL, ADC_ATTEN);
    gpio_reset_pin(RELAY_GPIO);
    gpio_set_direction(RELAY_GPIO, GPIO_MODE_OUTPUT);
    // gpio_set_level(RELAY_GPIO, 0);

    while (1)
    {
        humidity(client);
        vTaskDelay(pdMS_TO_TICKS(2000)); // wait 2 seconds
        gpio_set_level(RELAY_GPIO, 0);
        vTaskDelay(pdMS_TO_TICKS(2000)); // wait 2 seconds

    }
}
