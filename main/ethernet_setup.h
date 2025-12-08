#pragma once

#include "esp_err.h"

/**
 * @brief Initialize Ethernet driver and network interface
 * 
 * @return esp_err_t ESP_OK on success
 */
esp_err_t ethernet_setup_init(void);
