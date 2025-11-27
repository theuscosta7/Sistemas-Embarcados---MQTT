idf_component_register(
    SRCS "app_main.c"
   INCLUDE_DIRS "."
   EMBED_TXTFILES "certs/ca.pem"
   REQUIRES esp_event nvs_flash mqtt esp_wifi esp_driver_gpio
)
