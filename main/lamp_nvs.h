#ifndef LAMP_NVS_H
#define LAMP_NVS_H

#include "esp_err.h"

#define MAX_LAMP_NAME_LEN 32
#define MAX_LAMP_ADDR_LEN 8

// The core struct remains the same.
typedef struct {
    char name[MAX_LAMP_NAME_LEN];
    char address[MAX_LAMP_ADDR_LEN];
} LampInfo;

/**
 * @brief Initializes the lamp storage system.
 *
 * This must be called once at application startup. It loads the list of lamps
 * from NVS into an in-memory cache for fast access.
 */
void lamp_nvs_init(void);

/**
 * @brief Adds a new lamp to the storage.
 *
 * @param new_lamp Pointer to the LampInfo struct for the new lamp.
 * @return ESP_OK on success, or an error code on failure.
 */
esp_err_t add_lamp_info(const LampInfo *new_lamp);

/**
 * @brief Removes a lamp from storage by its name.
 *
 * @param name The name of the lamp to remove.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found.
 */
esp_err_t remove_lamp_info_by_name(const char *name);

/**
 * @brief Updates an existing lamp's information.
 *
 * @param original_name The current name of the lamp to be updated.
 * @param updated_lamp Pointer to a LampInfo struct with the new details.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found.
 */
esp_err_t update_lamp_info(const char *original_name, const LampInfo *updated_lamp);

/**
 * @brief Gets a pointer to the in-memory list of all lamps.
 *
 * @param[out] count Pointer to an integer that will be filled with the number of lamps.
 * @return A const pointer to the array of LampInfo structs. Do not modify this array directly.
 */
const LampInfo* get_all_lamps(int* count);

/**
 * @brief Finds a lamp by its name from the in-memory cache.
 *
 * @param name The name of the lamp to find.
 * @param[out] lamp_info Pointer to a LampInfo struct to be filled with the found data.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found.
 */
esp_err_t find_lamp_by_name(const char *name, LampInfo *lamp_info);

/**
 * @brief Finds a lamp by its address from the in-memory cache.
 *
 * @param address The unicast address of the lamp to find.
 * @param[out] lamp_info Pointer to a LampInfo struct to be filled with the found data.
 * @return ESP_OK on success, ESP_ERR_NOT_FOUND if not found.
 */
esp_err_t find_lamp_by_address(uint16_t address, LampInfo *lamp_info);

/**
 * @brief Gets the current number of stored lamps.
 *
 * @return The number of lamps.
 */
int get_lamp_count(void);

#endif // LAMP_NVS_H
