#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "esp_system.h"
#include "esp_err.h"
#include "esp_event.h"
#include "esp_netif.h"
#include "esp_log.h"
#include "nvs_flash.h"

#include "esp_wifi.h"
#include "mqtt_client.h"
#include "driver/gpio.h"

extern const uint8_t ca_pem_start[] asm("_binary_ca_pem_start");
extern const uint8_t ca_pem_end[] asm("_binary_ca_pem_end");

#define WIFI_CONNECTED_BIT BIT0
#define LED_PIN 4
#define BUTTON_PIN 17

static EventGroupHandle_t wifi_event_group;
static const char *TAG = "MQTT";
static esp_mqtt_client_handle_t mqtt_client;
static SemaphoreHandle_t led_mutex;
static bool led_state = 0;

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START)
    {
        esp_wifi_connect();
    }
    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED)
    {
        ESP_LOGW(TAG, "WiFi desconectado, tentando reconectar...");
        esp_wifi_connect();
    }
    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP)
    {
        ESP_LOGI(TAG, "WiFi conectado e IP recebido.");
        xEventGroupSetBits(wifi_event_group, WIFI_CONNECTED_BIT);
    }
}

static void wifi_init(void)
{
    wifi_event_group = xEventGroupCreate();

    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    esp_netif_create_default_wifi_sta();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID, wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP, wifi_event_handler, NULL));

    wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Adriel",
            .password = "21059846",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };

    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
    ESP_ERROR_CHECK(esp_wifi_start());

    ESP_LOGI(TAG, "WiFi iniciado");
}

static void set_led_state(bool state)
{
    if (xSemaphoreTake(led_mutex, portMAX_DELAY) == pdTRUE)
    {
        led_state = state;
        gpio_set_level(LED_PIN, led_state);
        xSemaphoreGive(led_mutex);
    }
}

static bool get_led_state(void)
{
    bool state = false;
    if (xSemaphoreTake(led_mutex, portMAX_DELAY) == pdTRUE)
    {
        state = led_state;
        xSemaphoreGive(led_mutex);
    }

    return state;
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data)
{
    esp_mqtt_event_handle_t event = event_data;

    switch (event_id)
    {
    case MQTT_EVENT_BEFORE_CONNECT:
        ESP_LOGI(TAG, "Tentando conectar ao MQTT");
        break;

    case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado");
        gpio_reset_pin(LED_PIN);
        gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

        set_led_state(0);

        int msg_id = esp_mqtt_client_subscribe(event->client, "/topic/led", 0);
        ESP_LOGI(TAG, "Inscrito no tópico /topic/led, msg_id: %d", msg_id);
        break;

    case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT desconectado");
        break;

    case MQTT_EVENT_SUBSCRIBED:
        ESP_LOGI(TAG, "Inscrição confirmada, msg_id: %d", event->msg_id);
        break;

    case MQTT_EVENT_DATA:
        ESP_LOGI(TAG, "Dados recebidos - Tópico: %.*s, Mensagem: %.*s",
                 event->topic_len, event->topic,
                 event->data_len, event->data);

        if (strncmp(event->topic, "/topic/led", event->topic_len) == 0)
        {
            if (strncmp(event->data, "ON", event->data_len) == 0)
            {
                set_led_state(1);
                ESP_LOGI(TAG, "LED LIGADO");
            }
            else if (strncmp(event->data, "OFF", event->data_len) == 0)
            {
                set_led_state(0);
                ESP_LOGI(TAG, "LED DESLIGADO");
            }
        }
        break;

    case MQTT_EVENT_ERROR:
        ESP_LOGE(TAG, "Erro MQTT");
        if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT)
        {
            ESP_LOGE(TAG, "Erro de transporte: %s", esp_err_to_name(event->error_handle->esp_transport_sock_errno));
        }
        break;

    default:
        ESP_LOGI(TAG, "Evento MQTT: %ld", event_id);
        break;
    }
}

static void mqtt_start(void)
{
    esp_mqtt_client_config_t mqtt5_cfg = {
        .broker = {
            .address = {
                .hostname = "338740a3d6d24dddb183daabd0482aa3.s1.eu.hivemq.cloud",
                .port = 8883,
                .transport = MQTT_TRANSPORT_OVER_SSL,
            },
            .verification = {
                .certificate = (const char *)ca_pem_start,
            }},
        .credentials = {.username = "esp32", .authentication = {
                                                 .password = "123456aA",
                                             }}};

    mqtt_client = esp_mqtt_client_init(&mqtt5_cfg);
    esp_mqtt_client_register_event(mqtt_client, ESP_EVENT_ANY_ID, mqtt_event_handler, NULL);
    esp_mqtt_client_start(mqtt_client);

    ESP_LOGI(TAG, "Cliente MQTT iniciado");
}

static void button_task(void *pvParameter)
{
    bool prevButtonState = 1;

    gpio_reset_pin(BUTTON_PIN);
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);

    ESP_LOGI(TAG, "botão iniciado");

    while (1)
    {
        int currentButtonState = gpio_get_level(BUTTON_PIN);

        if (currentButtonState != prevButtonState)
        {

            const char *message;

            if (currentButtonState == 0)
            {
                message = "ON";
                ESP_LOGI(TAG, "Botão pressionado");
            }
            else
            {
                message = "OFF";
                ESP_LOGI(TAG, "Botão solto");
            }
            

            if (currentButtonState == 0) {

                if (mqtt_client != NULL)
            {

                int msg_id = esp_mqtt_client_publish(mqtt_client, "/topic/button", message, 0, 1, 0);
                ESP_LOGI(TAG, "botao pressionado, LED: %s, msg_id: %d", message, msg_id);
            }

            }

            else {

                if (mqtt_client != NULL)
            {
                const char *message = led_state ? "ON" : "OFF";

                int msg_id = esp_mqtt_client_publish(mqtt_client, "/topic/button", message, 0, 1, 0);
                ESP_LOGI(TAG, "botao solto, LED: %s, msg_id: %d", message, msg_id);
            }

            }

            vTaskDelay(200 / portTICK_PERIOD_MS);
        }

        prevButtonState = currentButtonState;
        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}


void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());

    led_mutex = xSemaphoreCreateMutex();

    wifi_init();

    xEventGroupWaitBits(
        wifi_event_group,
        WIFI_CONNECTED_BIT,
        pdFALSE,
        pdTRUE,
        portMAX_DELAY);

    ESP_LOGI(TAG, "WiFi OK — iniciando MQTT...");

    mqtt_start();

    vTaskDelay(2000 / portTICK_PERIOD_MS);

    xTaskCreate(button_task, "button_task", 4096, NULL, 5, NULL);

    while (1)
    {
        vTaskDelay(pdMS_TO_TICKS(1000));
    }
}