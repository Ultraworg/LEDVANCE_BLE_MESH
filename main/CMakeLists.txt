set(srcs "main.c"
        "board.c")

idf_component_register(SRCS "http_server.c" "lamp_nvs.c" "${srcs}"
                    INCLUDE_DIRS  ".")