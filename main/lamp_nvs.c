#include "lamp_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include <string.h>

// Define the maximum number of lamps
#define MAX_LAMPS 20

#define TAG "LAMP_NVS"

// Function to save lamp information to NVS
esp_err_t save_lamp_info(LampInfo *lamp_info, int index) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open("lamps", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }

    // Write lamp info to NVS
    char key[20];
    snprintf(key, sizeof(key), "lamp%d_name", index);
    err = nvs_set_str(nvs_handle, key, lamp_info->name);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    snprintf(key, sizeof(key), "lamp%d_address", index);
    err = nvs_set_str(nvs_handle, key, lamp_info->address);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }
    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        return err;
    }

    // Close NVS handle
    nvs_close(nvs_handle);

    return ESP_OK;
}

// Function to load lamp information from NVS
esp_err_t load_lamp_info(LampInfo *lamp_info, int index) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open("lamps", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        return err;
    }
    //ESP_LOGI(TAG, "Loading lamp info");
    // Read lamp info from NVS
    char key[20];
    snprintf(key, sizeof(key), "lamp%d_name", index);
    size_t size = sizeof(lamp_info->name);
    err = nvs_get_str(nvs_handle, key, lamp_info->name, &size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
    //    ESP_LOGE(TAG, "Loading lamp info failed for name");
        return err;
    }
    snprintf(key, sizeof(key), "lamp%d_address", index);
    size = sizeof(lamp_info->address);
    err = nvs_get_str(nvs_handle, key, lamp_info->address, &size);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
    //    ESP_LOGE(TAG, "Loading lamp info failed for address");
        return err;
    }

    // Close NVS handle
    nvs_close(nvs_handle);
    //ESP_LOGI(TAG, "Loading done");
    return ESP_OK;
}

// Function to find the next free index in the NVS store
int findNextFreeIndexInNVS() {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open("lamps", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return -1;
    }

    // Iterate through the indices and check for gaps
    int nextFreeIndex = -1;
    for (int i = 0; i < MAX_LAMPS; i++) {
        char key[20];
        snprintf(key, sizeof(key), "lamp%d_name", i);
        size_t required_size;
        err = nvs_get_str(nvs_handle, key, NULL, &required_size);
        if (err == ESP_ERR_NVS_NOT_FOUND) {
            // Free index found
            nextFreeIndex = i;
            ESP_LOGI(TAG, "Free index found in NVS: %d", nextFreeIndex);
            break;
        } else if (err != ESP_OK) {
            ESP_LOGE(TAG, "Error reading NVS: %s", esp_err_to_name(err));
            // Error handling, return a negative value
            nextFreeIndex = -1;
            break;
        }
    }

    // Close the NVS handle
    nvs_close(nvs_handle);

    return nextFreeIndex;
}

// Function to remove lamp information from NVS
esp_err_t remove_lamp_info(int index) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open("lamps", NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Delete lamp info from NVS
    char key[20];
    snprintf(key, sizeof(key), "lamp%d_name", index);
    err = nvs_erase_key(nvs_handle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        ESP_LOGE(TAG, "Error deleting lamp name from NVS: %s", esp_err_to_name(err));
        return err;
    }

    snprintf(key, sizeof(key), "lamp%d_address", index);
    err = nvs_erase_key(nvs_handle, key);
    if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
        nvs_close(nvs_handle);
        ESP_LOGE(TAG, "Error deleting lamp address from NVS: %s", esp_err_to_name(err));
        return err;
    }

    // Commit changes
    err = nvs_commit(nvs_handle);
    if (err != ESP_OK) {
        nvs_close(nvs_handle);
        ESP_LOGE(TAG, "Error committing NVS changes: %s", esp_err_to_name(err));
        return err;
    }

    // Close NVS handle
    nvs_close(nvs_handle);

    return ESP_OK;
}

// Function to find the index of a lamp by its name or address
int find_index_by_name_or_address(const char *name, const char *address) {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open("lamps", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return -1;
    }

    // Iterate through all lamps
    for (int i = 0; i < MAX_LAMPS; i++) {
        LampInfo lamp_info;
        err = load_lamp_info(&lamp_info, i);
        if (err != ESP_OK) {
            continue; // Skip if error loading lamp info
        }

        // Check if name or address matches
        if ((name && strcmp(lamp_info.name, name) == 0) || (address && strcmp(lamp_info.address, address) == 0)) {
            nvs_close(nvs_handle);
            return i; // Return the index if found
        }
    }

    nvs_close(nvs_handle);
    return -1; // Return -1 if not found
}

// Function to print all lamp information to console
void printAllLampInfo() {
    nvs_handle_t nvs_handle;
    esp_err_t err;

    // Open NVS namespace
    err = nvs_open("lamps", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return;
    }

    // Iterate through all lamps
    for (int i = 0; i < MAX_LAMPS; i++) {
        // Load lamp information from NVS
        LampInfo lamp_info;
        err = load_lamp_info(&lamp_info, i);
        if (err != ESP_OK) {
            // If lamp not found or other error, skip to next lamp
            continue;
        }
          // Print lamp name and address
        ESP_LOGI(TAG, "Lamp %d - Name: %s, Address: %s", i, lamp_info.name, lamp_info.address);
    }

    // Close NVS handle
    nvs_close(nvs_handle);
}

int getCurrentNumberOfLamps() {
    nvs_handle_t nvs_handle;
    esp_err_t err;
    // Open NVS namespace
    err = nvs_open("lamps", NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS: %s", esp_err_to_name(err));
        return -1; // Return -1 to indicate error
    }
    // Iterate through the indices and count the number of lamps
    int count = 0;
    for (int i = 0; i < MAX_LAMPS; i++) {
        LampInfo lamp_info;
        err = load_lamp_info(&lamp_info, i);
        if (err == ESP_OK) {
            count++;
        //} else if (err == ESP_ERR_NVS_NOT_FOUND) {
            // No more lamps found, break the loop
        //    break;
        //} else {
        //    ESP_LOGE(TAG, "Error loading lamp info: %s", esp_err_to_name(err));
        //    nvs_close(nvs_handle);
        //    return -1; // Return -1 to indicate error
        }
    }
    
    // Close the NVS handle
    nvs_close(nvs_handle);
    return count;
}
