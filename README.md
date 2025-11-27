<h1>📝 Projeto ESP32 com MQTT (HiveMQ Cloud) </h1>
<h3> Alunos: Adriel Aigle e Mateus dos Santos </h3>

---

<h1> 📌 Descrição do Projeto </h1>

<h3> Esse projeto implementa uma comunicação MQTT via TLS entre o ESP32 e o HiveMQ Cloud, permitindo: </h3>
<li> Ligar e Desligar o LED remotamente pelo tópico "/topic/led" </li>
<li> Publicar o estado do botão no tópico "/topic/button" </li>

---

<h1> 🔧 Configuração do HiveMQ Cloud </h1>

<h3> Fizemos uso da porta 8883 no HiveMQ por conta de sua integridade com os dados, com isso foi necessário
realizar o download de um certificado para este uso, que permite o ESP32 validar o servidor e assim aceitar a conexão. As credenciais foram criadas no HiveMQ para que haja a conexão do dispositivo ao broker MQTT na nuvem.
</h3>

<pre> .broker = {
            .address = {
                .hostname = "338740a3d6d24dddb183daabd0482aa3.s1.eu.hivemq.cloud",
                .port = 8883,
                .transport = MQTT_TRANSPORT_OVER_SSL,
            },
            .verification = {
                .certificate = (const char *)ca_pem_start,
            }},
        .credentials = {.username = "esp32", 
                        .authentication = { .password = "123456aA",
                                             }}}; </pre>

---

<h1> 🌐 Configuração do Wifi </h1>
<h3> Dentro do método de "wifi_init" é onde tem a configuração e inicialização do módulo do Wi-Fi do ESP32, afinal, sem Wi-Fi, não existe MQTT.
</h3>

<pre> wifi_config_t wifi_config = {
        .sta = {
            .ssid = "Adriel",
            .password = "21059846",
            .threshold.authmode = WIFI_AUTH_WPA2_PSK,
        },
    };
</pre>

---

<h1> 💡 Configuração dos LEDs </h1>
<h3>Nesses métodos é onde ocorre a alteração e o retorno do estado do LED utilizando o mutex para que não haja conflito ao acessar o led, garantindo que o mesmo seja alterado corretamente tanto pelo MQTT quanto pelo botão. Alterações essas que atualizam na variável "led state". </h3>

<pre>
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
</pre>
---

<h1> 📡 Configuração dos eventos do MQTT </h1>
<h3> Aqui é onde são tratados os eventos do cliente MQTT, especificamente esses por serem importantes: </h3>
  <li> MQTT_EVENT_CONNECTED: Conexão com o broker, realiza a inscrição do ESP32 no tópico especifico e desse modo já receber os comandos da nuvem. </li>
  <li>MQTT_EVENT_DATA: Aqui são retornadas as mensagens do tópico inscrito, passar o nome do mesmo, o conteudo da mensagem e o tamanho da mensagem. </li>
  <li>MQTT_EVENT_DISCONNECTED: Apenas registra a informação que houve perca de conexão, mas automaticamente ja tenta reconectar </li>
  <li>MQTT_EVENT_ERROR: Caso aconteça de ocorrer um erro tanto na camada do MQTT quanto no transporte TLS, ele dispara esse evento. </li>

<pre> 
case MQTT_EVENT_CONNECTED:
        ESP_LOGI(TAG, "MQTT conectado");
        gpio_reset_pin(LED_PIN);
        gpio_set_direction(LED_PIN, GPIO_MODE_OUTPUT);

        set_led_state(0);

        int msg_id = esp_mqtt_client_subscribe(event->client, "/topic/led", 0);
        ESP_LOGI(TAG, "Inscrito no tópico /topic/led, msg_id: %d", msg_id);
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

  case MQTT_EVENT_DISCONNECTED:
        ESP_LOGI(TAG, "MQTT desconectado");
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
</pre>
---

<h1> 🔘 Configuração do botão </h1>
<h3> Monitoramento do botão no GPIO (// 1), Detecção da mudança de estado (// 2) e Verificação/Publicando (// 3) no MQTT se está pressionado/solto, permitindo que o ESP32 sincronize com as ações física com a nuvem. </h3>
<pre>
  gpio_reset_pin(BUTTON_PIN); // 1
    gpio_set_direction(BUTTON_PIN, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_PIN, GPIO_PULLUP_ONLY);
  
  while (1)
    {
        int currentButtonState = gpio_get_level(BUTTON_PIN);

        if (currentButtonState != prevButtonState) // 2
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

                int msg_id = esp_mqtt_client_publish(mqtt_client, "/topic/button", message, 0, 1, 0); // 3
                ESP_LOGI(TAG, "botao pressionado, LED: %s, msg_id: %d", message, msg_id);
            }

            }

            else {

                if (mqtt_client != NULL)
            {
                const char *message = led_state ? "ON" : "OFF";

                int msg_id = esp_mqtt_client_publish(mqtt_client, "/topic/button", message, 0, 1, 0); // 3
                ESP_LOGI(TAG, "botao solto, LED: %s, msg_id: %d", message, msg_id);
            }

            }
</pre>
---
