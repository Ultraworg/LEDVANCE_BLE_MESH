#include "lamp_nvs.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "esp_log.h"
#include "cJSON.h"
#include <string.h>
#include <stdlib.h>

#define MAX_LAMPS 20
#define TAG "LAMP_NVS"
#define NVS_NAMESPACE "lamps"
#define NVS_KEY "lamp_list"

// In-memory cache for fast access
static LampInfo g_lamp_cache[MAX_LAMPS];
static int g_lamp_count = 0;

// Forward declaration for internal function
static esp_err_t _save_to_nvs(void);

/**
 * @brief Loads the lamp list from the NVS blob into the in-memory cache.
 */
static void _load_from_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGI(TAG, "NVS namespace not found, initializing empty lamp list.");
        g_lamp_count = 0;
        return;
    }

    size_t required_size = 0;
    err = nvs_get_blob(nvs_handle, NVS_KEY, NULL, &required_size);
    if (err == ESP_ERR_NVS_NOT_FOUND || required_size == 0) {
        ESP_LOGI(TAG, "Lamp list not found in NVS, initializing empty list.");
        g_lamp_count = 0;
        nvs_close(nvs_handle);
        return;
    }

    char* json_string = malloc(required_size);
    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to allocate memory for lamp list JSON!");
        g_lamp_count = 0;
        nvs_close(nvs_handle);
        return;
    }

    err = nvs_get_blob(nvs_handle, NVS_KEY, json_string, &required_size);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read lamp list blob from NVS: %s", esp_err_to_name(err));
        g_lamp_count = 0;
    } else {
        cJSON *root = cJSON_Parse(json_string);
        if (root != NULL && cJSON_IsArray(root)) {
            int count = 0;
            cJSON *elem;
            cJSON_ArrayForEach(elem, root) {
                if (count >= MAX_LAMPS) break;
                cJSON *name = cJSON_GetObjectItem(elem, "name");
                cJSON *address = cJSON_GetObjectItem(elem, "address");
                if (cJSON_IsString(name) && cJSON_IsString(address)) {
                    strncpy(g_lamp_cache[count].name, name->valuestring, MAX_LAMP_NAME_LEN - 1);
                    strncpy(g_lamp_cache[count].address, address->valuestring, MAX_LAMP_ADDR_LEN - 1);
                    g_lamp_cache[count].name[MAX_LAMP_NAME_LEN - 1] = '\0';
                    g_lamp_cache[count].address[MAX_LAMP_ADDR_LEN - 1] = '\0';
                    count++;
                }
            }
            g_lamp_count = count;
            ESP_LOGI(TAG, "Loaded %d lamps from NVS.", g_lamp_count);
        } else {
            ESP_LOGW(TAG, "Failed to parse lamp list JSON, starting fresh.");
            g_lamp_count = 0;
        }
        cJSON_Delete(root);
    }

    free(json_string);
    nvs_close(nvs_handle);
}

/**
 * @brief Saves the in-memory lamp cache to the NVS blob.
 */
static esp_err_t _save_to_nvs(void) {
    nvs_handle_t nvs_handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &nvs_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error opening NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    cJSON *root = cJSON_CreateArray();
    for (int i = 0; i < g_lamp_count; i++) {
        cJSON *lamp_obj = cJSON_CreateObject();
        cJSON_AddStringToObject(lamp_obj, "name", g_lamp_cache[i].name);
        cJSON_AddStringToObject(lamp_obj, "address", g_lamp_cache[i].address);
        cJSON_AddItemToArray(root, lamp_obj);
    }

    char *json_string = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);

    if (json_string == NULL) {
        ESP_LOGE(TAG, "Failed to create JSON string from lamp cache.");
        nvs_close(nvs_handle);
        return ESP_FAIL;
    }

    err = nvs_set_blob(nvs_handle, NVS_KEY, json_string, strlen(json_string) + 1);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to set lamp list blob in NVS: %s", esp_err_to_name(err));
    } else {
        err = nvs_commit(nvs_handle);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Failed to commit NVS changes: %s", esp_err_to_name(err));
        } else {
            ESP_LOGI(TAG, "Successfully saved %d lamps to NVS.", g_lamp_count);
        }
    }

    free(json_string);
    nvs_close(nvs_handle);
    return err;
}

// --- Public API Functions ---

void lamp_nvs_init(void) {
    _load_from_nvs();
}

esp_err_t add_lamp_info(const LampInfo *new_lamp) {
    if (g_lamp_count >= MAX_LAMPS) {
        ESP_LOGE(TAG, "Cannot add lamp, storage is full.");
        return ESP_ERR_NVS_NO_FREE_PAGES;
    }
    // Check for duplicate name or address before adding
    for (int i = 0; i < g_lamp_count; i++) {
        if (strcmp(g_lamp_cache[i].name, new_lamp->name) == 0) {
            ESP_LOGE(TAG, "Cannot add lamp, name '%s' already exists.", new_lamp->name);
            return ESP_ERR_INVALID_ARG;
        }
    }

    memcpy(&g_lamp_cache[g_lamp_count], new_lamp, sizeof(LampInfo));
    g_lamp_count++;
    return _save_to_nvs();
}

esp_err_t remove_lamp_info_by_name(const char *name) {
    int found_index = -1;
    for (int i = 0; i < g_lamp_count; i++) {
        if (strcmp(g_lamp_cache[i].name, name) == 0) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    // Shift elements to fill the gap
    for (int i = found_index; i < g_lamp_count - 1; i++) {
        memcpy(&g_lamp_cache[i], &g_lamp_cache[i + 1], sizeof(LampInfo));
    }
    g_lamp_count--;

    return _save_to_nvs();
}

esp_err_t update_lamp_info(const char *original_name, const LampInfo *updated_lamp) {
    int found_index = -1;
    for (int i = 0; i < g_lamp_count; i++) {
        if (strcmp(g_lamp_cache[i].name, original_name) == 0) {
            found_index = i;
            break;
        }
    }

    if (found_index == -1) {
        return ESP_ERR_NVS_NOT_FOUND;
    }

    memcpy(&g_lamp_cache[found_index], updated_lamp, sizeof(LampInfo));
    return _save_to_nvs();
}

const LampInfo* get_all_lamps(int* count) {
    *count = g_lamp_count;
    return g_lamp_cache;
}

esp_err_t find_lamp_by_name(const char *name, LampInfo *lamp_info) {
    for (int i = 0; i < g_lamp_count; i++) {
        if (strcmp(g_lamp_cache[i].name, name) == 0) {
            memcpy(lamp_info, &g_lamp_cache[i], sizeof(LampInfo));
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t find_lamp_by_address(uint16_t address, LampInfo *lamp_info) {
    for (int i = 0; i < g_lamp_count; i++) {
        uint16_t stored_addr = (uint16_t)strtol(g_lamp_cache[i].address, NULL, 0);
        if (stored_addr == address) {
            memcpy(lamp_info, &g_lamp_cache[i], sizeof(LampInfo));
            return ESP_OK;
        }
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

int get_lamp_count(void) {
    return g_lamp_count;
}