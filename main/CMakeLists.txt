set(srcs "main.c"
        "board.c"
        "lamp_nvs.c"
        "http_server.c"
        "wifi_setup.c")

idf_component_register(SRCS "ethernet_setup.c" "${srcs}"
                    INCLUDE_DIRS "."
                    REQUIRES nvs_flash esp_wifi esp_event driver mqtt esp_http_server json bt)
