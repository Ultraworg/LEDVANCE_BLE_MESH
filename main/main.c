/*
 * main.c - Application main entry point
 *
 * This file is a refactored version of the original code, focusing on:
 * - Improved code structure and readability.
 * - Fixing a critical memory leak in the MQTT payload creation.
 * - More robust and efficient MQTT topic handling.
 * - Safer string and memory operations.
 * - Enhanced error handling and logging.
 *
 * SPDX-FileCopyrightText: 2017 Intel Corporation
 * SPDX-FileContributor: 2018-2021 Espressif Systems (Shanghai) CO LTD
 * SPDX-FileContributor: 2024 Gemini
 *
 * SPDX-License-Identifier: Apache-2.0
 */

/* Standard C Libraries */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* FreeRTOS */
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"

/* ESP-IDF Core */
#include "esp_bt.h"
#include "esp_bt_main.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs.h"
#include "nvs_flash.h"

/* Wi-Fi & Networking */
#include "esp_netif.h"

/* MQTT */
#include "mqtt_client.h"

/* BLE Mesh */
#include "esp_ble_mesh_common_api.h"
#include "esp_ble_mesh_config_model_api.h"
#include "esp_ble_mesh_generic_model_api.h"
#include "esp_ble_mesh_lighting_model_api.h"
#include "esp_ble_mesh_networking_api.h"
#include "esp_ble_mesh_provisioning_api.h"

/* Project-specific */
#include "board.h"
#include "cJSON.h"
#include "ethernet_setup.h"
#include "http_server.h"
#include "lamp_nvs.h"
#include "main.h"
#include "wifi_setup.h"

/* --- Macros and Constants --- */

// Static buffers to hold MQTT config to ensure they persist
static char s_mqtt_url[128] = {0};
static char s_mqtt_user[64] = {0};
static char s_mqtt_pass[64] = {0};

// General
#define TAG "BLE_MESH_GATEWAY"
#define CID_ESP 0x02E5

// Wi-Fi Configuration (from menuconfig)
// #define ESP_WIFI_SSID      CONFIG_ESP_WIFI_SSID
// #define ESP_WIFI_PASS      CONFIG_ESP_WIFI_PASSWORD
#define ESP_MAXIMUM_RETRY CONFIG_ESP_MAXIMUM_RETRY

// MQTT Configuration (from menuconfig)
#define MQTT_BROKER_URL CONFIG_BROKER_URL
#define MQTT_USER CONFIG_USERNAME_MQTT
#define MQTT_PASS CONFIG_PASSWORD_MQTT

// NVS Keys
#define NVS_MESH_INFO_KEY "mesh_info"

// FreeRTOS Event Group bits for Wi-Fi
#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT BIT1

/* --- Global Variables --- */

// BLE Mesh
static uint8_t dev_uuid[16] = {0xcc, 0xcc};
static esp_ble_mesh_client_t onoff_client;
static esp_ble_mesh_client_t level_client;
static esp_ble_mesh_client_t light_client;

// MQTT
static esp_mqtt_client_handle_t mqtt_client = NULL;

// NVS
static nvs_handle_t NVS_HANDLE;

// Application State
static struct app_state_t {
  uint16_t net_idx; /* NetKey Index */
  uint16_t app_idx; /* AppKey Index */
  uint8_t tid;      /* Message TID */
} __attribute__((packed)) app_state = {
    .net_idx = ESP_BLE_MESH_KEY_UNUSED,
    .app_idx = ESP_BLE_MESH_KEY_UNUSED,
    .tid = 0x0,
};

/* --- BLE Mesh Configuration --- */

static esp_ble_mesh_cfg_srv_t config_server = {
    .relay = ESP_BLE_MESH_RELAY_DISABLED,
    .beacon = ESP_BLE_MESH_BEACON_ENABLED,
#if defined(CONFIG_BLE_MESH_FRIEND)
    .friend_state = ESP_BLE_MESH_FRIEND_ENABLED,
#else
    .friend_state = ESP_BLE_MESH_FRIEND_NOT_SUPPORTED,
#endif
#if defined(CONFIG_BLE_MESH_GATT_PROXY_SERVER)
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_ENABLED,
#else
    .gatt_proxy = ESP_BLE_MESH_GATT_PROXY_NOT_SUPPORTED,
#endif
    .default_ttl = 7,
    .net_transmit = ESP_BLE_MESH_TRANSMIT(2, 20),
    .relay_retransmit = ESP_BLE_MESH_TRANSMIT(2, 20),
};

ESP_BLE_MESH_MODEL_PUB_DEFINE(onoff_cli_pub, 2 + 2, ROLE_NODE);
ESP_BLE_MESH_MODEL_PUB_DEFINE(level_cli_pub, 2 + 2, ROLE_NODE);
ESP_BLE_MESH_MODEL_PUB_DEFINE(light_cli_pub, 2 + 2, ROLE_NODE);

static esp_ble_mesh_model_t root_models[] = {
    ESP_BLE_MESH_MODEL_CFG_SRV(&config_server),
    ESP_BLE_MESH_MODEL_GEN_ONOFF_CLI(&onoff_cli_pub, &onoff_client),
    ESP_BLE_MESH_MODEL_GEN_LEVEL_CLI(&level_cli_pub, &level_client),
    ESP_BLE_MESH_MODEL_LIGHT_LIGHTNESS_CLI(&light_cli_pub, &light_client),
};

static esp_ble_mesh_elem_t elements[] = {
    ESP_BLE_MESH_ELEMENT(0, root_models, ESP_BLE_MESH_MODEL_NONE),
};

static esp_ble_mesh_comp_t composition = {
    .cid = CID_ESP,
    .elements = elements,
    .element_count = ARRAY_SIZE(elements),
};

static esp_ble_mesh_prov_t provision = {
    .uuid = dev_uuid,
    .output_size = 0,
    .output_actions = 0,
};

/* --- NVS Functions for BLE Mesh State --- */

static esp_err_t ble_mesh_nvs_open(nvs_handle_t *handle) {
  return nvs_open("ble_mesh", NVS_READWRITE, handle);
}

static esp_err_t ble_mesh_nvs_store(nvs_handle_t handle, const char *key,
                                    const void *data, size_t length) {
  return nvs_set_blob(handle, key, data, length);
}

static esp_err_t ble_mesh_nvs_restore(nvs_handle_t handle, const char *key,
                                      void *data, size_t length, bool *exist) {
  esp_err_t err = nvs_get_blob(handle, key, data, &length);
  if (err != ESP_OK) {
    if (err == ESP_ERR_NVS_NOT_FOUND) {
      *exist = false;
    }
    return err;
  }
  *exist = true;
  return ESP_OK;
}

static void mesh_info_store(void) {
  ble_mesh_nvs_store(NVS_HANDLE, NVS_MESH_INFO_KEY, &app_state,
                     sizeof(app_state));
}

static void mesh_info_restore(void) {
  esp_err_t err = ESP_OK;
  bool exist = false;

  err = ble_mesh_nvs_restore(NVS_HANDLE, NVS_MESH_INFO_KEY, &app_state,
                             sizeof(app_state), &exist);
  if (err != ESP_OK && err != ESP_ERR_NVS_NOT_FOUND) {
    ESP_LOGE(TAG, "Failed to restore mesh info from NVS");
    return;
  }

  if (exist) {
    ESP_LOGI(TAG, "Restored state: net_idx 0x%04x, app_idx 0x%04x, tid 0x%02x",
             app_state.net_idx, app_state.app_idx, app_state.tid);
  }
}

/* --- BLE Mesh Core Functions & Callbacks --- */

static void prov_complete(uint16_t net_idx, uint16_t addr, uint8_t flags,
                          uint32_t iv_index) {
  ESP_LOGI(TAG, "Provisioning complete: net_idx 0x%04x, addr 0x%04x", net_idx,
           addr);
  ESP_LOGI(TAG, "flags: 0x%02x, iv_index: 0x%08" PRIx32, flags, iv_index);

  board_led_operation(GPIO_NUM_2, LED_OFF);
  app_state.net_idx = net_idx;
}

static void ble_mesh_provisioning_cb(esp_ble_mesh_prov_cb_event_t event,
                                     esp_ble_mesh_prov_cb_param_t *param) {
  switch (event) {
  case ESP_BLE_MESH_PROV_REGISTER_COMP_EVT:
    ESP_LOGI(TAG, "ESP_BLE_MESH_PROV_REGISTER_COMP_EVT, err_code %d",
             param->prov_register_comp.err_code);
    mesh_info_restore();
    break;
  case ESP_BLE_MESH_NODE_PROV_LINK_OPEN_EVT:
    ESP_LOGI(TAG, "Provisioning link opened on %s",
             param->node_prov_link_open.bearer == ESP_BLE_MESH_PROV_ADV
                 ? "PB-ADV"
                 : "PB-GATT");
    break;
  case ESP_BLE_MESH_NODE_PROV_LINK_CLOSE_EVT:
    ESP_LOGI(TAG, "Provisioning link closed on %s",
             param->node_prov_link_close.bearer == ESP_BLE_MESH_PROV_ADV
                 ? "PB-ADV"
                 : "PB-GATT");
    break;
  case ESP_BLE_MESH_NODE_PROV_COMPLETE_EVT:
    prov_complete(
        param->node_prov_complete.net_idx, param->node_prov_complete.addr,
        param->node_prov_complete.flags, param->node_prov_complete.iv_index);
    break;
  default:
    break;
  }
}

static void
ble_mesh_config_server_cb(esp_ble_mesh_cfg_server_cb_event_t event,
                          esp_ble_mesh_cfg_server_cb_param_t *param) {
  if (event == ESP_BLE_MESH_CFG_SERVER_STATE_CHANGE_EVT) {
    switch (param->ctx.recv_op) {
    case ESP_BLE_MESH_MODEL_OP_APP_KEY_ADD:
      ESP_LOGI(TAG, "AppKey added: net_idx 0x%04x, app_idx 0x%04x",
               param->value.state_change.appkey_add.net_idx,
               param->value.state_change.appkey_add.app_idx);
      break;
    case ESP_BLE_MESH_MODEL_OP_MODEL_APP_BIND:
      ESP_LOGI(TAG,
               "Model bound: elem_addr 0x%04x, app_idx 0x%04x, mod_id 0x%04x",
               param->value.state_change.mod_app_bind.element_addr,
               param->value.state_change.mod_app_bind.app_idx,
               param->value.state_change.mod_app_bind.model_id);
      if (param->value.state_change.mod_app_bind.model_id ==
              ESP_BLE_MESH_MODEL_ID_GEN_ONOFF_CLI ||
          param->value.state_change.mod_app_bind.model_id ==
              ESP_BLE_MESH_MODEL_ID_LIGHT_LIGHTNESS_CLI) {
        app_state.app_idx = param->value.state_change.mod_app_bind.app_idx;
        mesh_info_store();
      }
      break;
    default:
      break;
    }
  }
}

static void
ble_mesh_generic_client_cb(esp_ble_mesh_generic_client_cb_event_t event,
                           esp_ble_mesh_generic_client_cb_param_t *param) {
  if (param->error_code) {
    ESP_LOGE(
        TAG,
        "Generic client error: event %u, error_code %d, opcode 0x%04" PRIx32,
        event, param->error_code, param->params->opcode);
    return;
  }

  switch (event) {
  case ESP_BLE_MESH_GENERIC_CLIENT_GET_STATE_EVT:
  case ESP_BLE_MESH_GENERIC_CLIENT_SET_STATE_EVT:
  case ESP_BLE_MESH_GENERIC_CLIENT_PUBLISH_EVT:
    if (param->params->ctx.recv_op == ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_STATUS) {
      uint16_t sender_addr = param->params->ctx.addr;
      uint8_t onoff_state = param->status_cb.onoff_status.present_onoff;
      ESP_LOGI(TAG, "OnOff status from 0x%04X: %s", sender_addr,
               onoff_state ? "ON" : "OFF");

      LampInfo lamp_info;
      if (find_lamp_by_address(sender_addr, &lamp_info) == ESP_OK) {
        char state_topic[256];
        char state_payload[128];
        snprintf(state_topic, sizeof(state_topic),
                 "homeassistant/light/%s/state", lamp_info.name);
        snprintf(state_payload, sizeof(state_payload), "{\"state\":\"%s\"}",
                 onoff_state ? "ON" : "OFF");
        if (mqtt_client) {
          esp_mqtt_client_publish(mqtt_client, state_topic, state_payload, 0, 0,
                                  false);
        }
      }
    }
    break;
  default:
    ESP_LOGD(TAG, "Unknown generic client event %u", event);
    break;
  }
}

static void
ble_mesh_lighting_client_cb(esp_ble_mesh_light_client_cb_event_t event,
                            esp_ble_mesh_light_client_cb_param_t *param) {
  ESP_LOGI(TAG, "Lighting client event: %d, error_code %d, opcode 0x%04" PRIx32,
           event, param->error_code, param->params->opcode);

  if (param->error_code) {
    ESP_LOGE(TAG, "Lighting client message failed to send (timeout)");
    return;
  }

  switch (event) {
  case ESP_BLE_MESH_LIGHT_CLIENT_GET_STATE_EVT:
  case ESP_BLE_MESH_LIGHT_CLIENT_SET_STATE_EVT:
  case ESP_BLE_MESH_LIGHT_CLIENT_PUBLISH_EVT:
    if (param->params->ctx.recv_op ==
        ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_STATUS) {
      uint16_t sender_addr = param->params->ctx.addr;
      uint16_t lightness = param->status_cb.lightness_status.present_lightness;
      ESP_LOGI(TAG, "Lightness status from 0x%04X: %d", sender_addr, lightness);
      // Here you could update Home Assistant with the actual brightness if
      // needed
    }
    // else if (param->params->ctx.recv_op ==
    // ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_STATUS) {
    //     uint16_t sender_addr = param->params->ctx.addr;
    //     uint16_t h = param->status_cb.hsl_status.hsl_hue;
    //     uint16_t s = param->status_cb.hsl_status.hsl_saturation;
    //     uint16_t lightness = param->status_cb.hsl_status.hsl_lightness;
    //     ESP_LOGI(TAG, "hsl status from 0x%04X: %d %d %d", sender_addr,
    //     h,s,lightness);
    //     // Here you could update Home Assistant with the actual hsl, but
    //     light does not appear to send these updates on boot.
    // }
    break;
  default:
    break;
  }
}

static esp_err_t bluetooth_init(void) {
  esp_err_t err;

  ESP_ERROR_CHECK(esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT));

  esp_bt_controller_config_t bt_cfg = BT_CONTROLLER_INIT_CONFIG_DEFAULT();
  err = esp_bt_controller_init(&bt_cfg);
  if (err) {
    ESP_LOGE(TAG, "Bluetooth controller initialize failed: %s",
             esp_err_to_name(err));
    return err;
  }

  err = esp_bt_controller_enable(ESP_BT_MODE_BLE);
  if (err) {
    ESP_LOGE(TAG, "Bluetooth controller enable failed: %s",
             esp_err_to_name(err));
    return err;
  }

  err = esp_bluedroid_init();
  if (err) {
    ESP_LOGE(TAG, "Bluedroid init failed: %s", esp_err_to_name(err));
    return err;
  }

  err = esp_bluedroid_enable();
  if (err) {
    ESP_LOGE(TAG, "Bluedroid enable failed: %s", esp_err_to_name(err));
    return err;
  }

  ESP_LOGI(TAG, "Bluetooth initialized");
  return ESP_OK;
}

static void ble_mesh_get_dev_uuid(uint8_t *dev_uuid) {
  if (dev_uuid == NULL) {
    ESP_LOGE(TAG, "%s, Invalid device uuid", __func__);
    return;
  }
  ESP_ERROR_CHECK(esp_efuse_mac_get_default(dev_uuid));
}

static esp_err_t ble_mesh_init(void) {
  esp_err_t err;
  esp_ble_mesh_register_prov_callback(ble_mesh_provisioning_cb);
  esp_ble_mesh_register_config_server_callback(ble_mesh_config_server_cb);
  esp_ble_mesh_register_generic_client_callback(ble_mesh_generic_client_cb);
  esp_ble_mesh_register_light_client_callback(ble_mesh_lighting_client_cb);

  err = esp_ble_mesh_init(&provision, &composition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to initialize mesh stack (err %d)", err);
    return err;
  }
  err = esp_ble_mesh_node_prov_enable(ESP_BLE_MESH_PROV_ADV |
                                      ESP_BLE_MESH_PROV_GATT);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to enable mesh node (err %d)", err);
    return err;
  }
  ESP_LOGI(TAG, "BLE Mesh Node initialized");
  board_led_operation(GPIO_NUM_2, LED_OFF);
  return ESP_OK;
}

/* --- BLE Mesh Client Send Functions --- */

void ble_mesh_send_gen_onoff_set(uint8_t onoff, uint16_t addr) {
  esp_ble_mesh_client_common_param_t common = {0};
  esp_ble_mesh_generic_client_set_state_t set = {0};
  esp_err_t err;

  if (app_state.app_idx == ESP_BLE_MESH_KEY_UNUSED) {
    ESP_LOGE(TAG, "Cannot send OnOff Set: AppKey has not been bound yet!");
    return;
  }
  ESP_LOGI(TAG,
           "Sending OnOff Set: onoff=%d, addr=0x%04X, net_idx=0x%04x, "
           "app_idx=0x%04x",
           onoff, addr, app_state.net_idx, app_state.app_idx);

  common.opcode = ESP_BLE_MESH_MODEL_OP_GEN_ONOFF_SET_UNACK;
  common.model = onoff_client.model;
  common.ctx.net_idx = app_state.net_idx;
  common.ctx.app_idx = app_state.app_idx;
  common.ctx.addr = addr;
  common.ctx.send_ttl = 7;
  common.ctx.send_rel = false;
  common.msg_timeout = 0;
  common.msg_role = ROLE_NODE;

  set.onoff_set.op_en = false;
  set.onoff_set.onoff = onoff;
  set.onoff_set.tid = app_state.tid++;

  err = esp_ble_mesh_generic_client_set_state(&common, &set);
  if (err) {
    ESP_LOGE(TAG, "Failed to send OnOff Set message (err %d)", err);
  }
}

void ble_mesh_send_lightness_set(uint16_t lightness, uint16_t addr) {
  esp_ble_mesh_client_common_param_t common = {0};
  esp_ble_mesh_light_client_set_state_t set = {0};
  esp_err_t err;

  if (app_state.app_idx == ESP_BLE_MESH_KEY_UNUSED) {
    ESP_LOGE(TAG, "Cannot send Lightness Set: AppKey has not been bound yet!");
    return;
  }

  ESP_LOGI(TAG,
           "Sending Lightness Set: lightness=%d, addr=0x%04X, net_idx=0x%04x, "
           "app_idx=0x%04x",
           lightness, addr, app_state.net_idx, app_state.app_idx);

  common.opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_LIGHTNESS_SET_UNACK;
  common.model = light_client.model;
  common.ctx.net_idx = app_state.net_idx;
  common.ctx.app_idx = app_state.app_idx;
  common.ctx.addr = addr;
  common.ctx.send_ttl = 7;
  common.ctx.send_rel = false;
  common.msg_timeout = 0;
  common.msg_role = ROLE_NODE;

  set.lightness_set.op_en = false;
  set.lightness_set.lightness = lightness;
  set.lightness_set.tid = app_state.tid++;

  err = esp_ble_mesh_light_client_set_state(&common, &set);
  if (err) {
    ESP_LOGE(TAG, "Failed to send Lightness Set message (err %d)", err);
  }
}

void ble_mesh_send_hsl_set(uint16_t hue, uint16_t saturation, uint16_t addr) {
  esp_ble_mesh_client_common_param_t common = {0};
  esp_ble_mesh_light_client_set_state_t set = {0};
  esp_err_t err;

  if (app_state.app_idx == ESP_BLE_MESH_KEY_UNUSED) {
    ESP_LOGE(TAG, "Cannot send hsl Set: AppKey has not been bound yet!");
    return;
  }

  ESP_LOGI(TAG,
           "Sending hsl Set: hue=%d, sat=%d, addr=0x%04X, net_idx=0x%04x, "
           "app_idx=0x%04x",
           hue, saturation, addr, app_state.net_idx, app_state.app_idx);

  common.opcode = ESP_BLE_MESH_MODEL_OP_LIGHT_HSL_SET_UNACK;
  common.model = light_client.model;
  common.ctx.net_idx = app_state.net_idx;
  common.ctx.app_idx = app_state.app_idx;
  common.ctx.addr = addr;
  common.ctx.send_ttl = 7;
  common.ctx.send_rel = false;
  common.msg_timeout = 0;
  common.msg_role = ROLE_NODE;

  set.hsl_set.op_en = false;
  set.hsl_set.hsl_lightness = 70;
  set.hsl_set.hsl_hue = hue;
  set.hsl_set.hsl_saturation = saturation;
  set.hsl_set.tid = app_state.tid++;

  err = esp_ble_mesh_light_client_set_state(&common, &set);
  if (err) {
    ESP_LOGE(TAG, "Failed to send hsl Set message (err %d)", err);
  }
}

/* --- MQTT Functions --- */

void refresh_mqtt_subscriptions(void) {
  if (mqtt_client == NULL) {
    ESP_LOGE(TAG, "Cannot refresh subscriptions, MQTT client not initialized.");
    return;
  }
  ESP_LOGI(TAG, "Refreshing MQTT subscriptions...");

  int lamp_count = 0;
  const LampInfo *lamps = get_all_lamps(&lamp_count);

  for (int i = 0; i < lamp_count; i++) {
    char topic[256];
    snprintf(topic, sizeof(topic), "homeassistant/light/%s/set", lamps[i].name);
    esp_mqtt_client_subscribe(mqtt_client, topic, 0);
    ESP_LOGI(TAG, "Subscribed to %s", topic);
  }
}

static char *create_ha_discovery_payload(const LampInfo *lamp,
                                         const char *base_topic) {
  cJSON *root = cJSON_CreateObject();
  if (root == NULL) {
    ESP_LOGE(TAG, "Failed to create cJSON root object");
    return NULL;
  }

  cJSON_AddNullToObject(root, "name");

  cJSON_AddStringToObject(root, "~", base_topic);
  cJSON_AddStringToObject(root, "cmd_t", "~/set");
  cJSON_AddStringToObject(root, "stat_t", "~/state");
  cJSON_AddStringToObject(root, "schema", "json");
  cJSON_AddTrueToObject(root, "brightness");
  // Use the lamp's specific brightness scaling
  cJSON_AddNumberToObject(root, "bri_scl", lamp->brightness_scaling);

  // Conditionally add color support
  if (lamp->supports_color) {
    cJSON_AddStringToObject(root, "sup_clrm", "hs");
  }
  cJSON_AddStringToObject(root, "uniq_id", lamp->address);

  cJSON *dev = cJSON_CreateObject();
  cJSON_AddStringToObject(dev, "name", lamp->name);
  cJSON_AddStringToObject(dev, "identifiers", lamp->address);
  cJSON_AddStringToObject(dev, "manufacturer", "Espressif");
  cJSON_AddStringToObject(dev, "model", "BLE Mesh Lamp");
  cJSON_AddItemToObject(root, "dev", dev);

  char *payload_str = cJSON_PrintUnformatted(root);
  cJSON_Delete(root);
  return payload_str;
}

void publish_ha_discovery_messages(void) {
  ESP_LOGI(TAG, "Publishing Home Assistant discovery messages...");
  int lamp_count = 0;
  const LampInfo *lamps = get_all_lamps(&lamp_count);

  for (int i = 0; i < lamp_count; i++) {
    char base_topic[256];
    char config_topic[256];
    snprintf(base_topic, sizeof(base_topic), "homeassistant/light/%s",
             lamps[i].name);
    snprintf(config_topic, sizeof(config_topic),
             "homeassistant/light/%s/config", lamps[i].name);

    char *payload = create_ha_discovery_payload(&lamps[i], base_topic);
    if (payload) {
      ESP_LOGI(TAG, "Publishing to %s", config_topic);
      esp_mqtt_client_publish(mqtt_client, config_topic, payload, 0, 1, true);
      free(payload);
    }
  }
}

static void handle_lamp_command(esp_mqtt_event_handle_t event) {
  char lamp_name[32] = {0};
  sscanf(event->topic, "homeassistant/light/%31[^/]/set", lamp_name);

  if (strlen(lamp_name) == 0) {
    ESP_LOGW(TAG, "Could not parse lamp name from topic: %.*s",
             event->topic_len, event->topic);
    return;
  }

  LampInfo lamp_info;
  if (find_lamp_by_name(lamp_name, &lamp_info) != ESP_OK) {
    ESP_LOGW(TAG, "Received command for unknown lamp: %s", lamp_name);
    return;
  }

  uint16_t addr = (uint16_t)strtol(lamp_info.address, NULL, 0);
  ESP_LOGI(TAG, "Command for lamp '%s' (addr 0x%04X)", lamp_name, addr);

  cJSON *json = cJSON_ParseWithLength(event->data, event->data_len);
  if (json == NULL) {
    ESP_LOGE(TAG, "Failed to parse command JSON");
    return;
  }
  // else
  // {
  //     ESP_LOG(TAG, "Got json: %s",cJSON_Print(json));
  // }

  char state_topic[256];
  snprintf(state_topic, sizeof(state_topic), "homeassistant/light/%s/state",
           lamp_name);
  char state_payload[128];

  const cJSON *brightness =
      cJSON_GetObjectItemCaseSensitive(json, "brightness");
  const cJSON *state = cJSON_GetObjectItemCaseSensitive(json, "state");
  const cJSON *temp = cJSON_GetObjectItemCaseSensitive(json, "color");
  const cJSON *hue = cJSON_GetObjectItemCaseSensitive(temp, "h");
  const cJSON *sat = cJSON_GetObjectItemCaseSensitive(temp, "s");

  // Prioritize brightness command, as it implies the light should be on.
  if (cJSON_IsNumber(hue) && cJSON_IsNumber(sat)) {
    int huenum = hue->valueint;
    int satnum = sat->valueint;
    uint16_t hue16 = (uint16_t)huenum;
    uint16_t sat16 = (uint16_t)satnum;
    ble_mesh_send_hsl_set(hue16, sat16, addr);
    snprintf(state_payload, sizeof(state_payload),
             "{\"state\":\"ON\", \"color\":{\"h\":%d,\"s\"%d}:}", hue16, sat16);
    esp_mqtt_client_publish(mqtt_client, state_topic, state_payload, 0, 0,
                            false);
  }
  if (cJSON_IsNumber(brightness)) {
    int bri_ha = brightness->valueint; // HA brightness 0-255

    // It's likely the lamp expects a 0-255 value, not the standard 0-65535.
    // We will send the value from Home Assistant directly.
    uint16_t lightness = (uint16_t)bri_ha;

    ble_mesh_send_lightness_set(lightness, addr);

    snprintf(state_payload, sizeof(state_payload),
             "{\"state\":\"ON\", \"brightness\":%d}", bri_ha);
    esp_mqtt_client_publish(mqtt_client, state_topic, state_payload, 0, 0,
                            false);
  }
  // If no brightness command, check for a state command.
  else if (cJSON_IsString(state) && (state->valuestring != NULL)) {
    if (strcmp(state->valuestring, "ON") == 0) {
      ble_mesh_send_gen_onoff_set(1, addr);
      snprintf(state_payload, sizeof(state_payload), "{\"state\":\"ON\"}");
      esp_mqtt_client_publish(mqtt_client, state_topic, state_payload, 0, 0,
                              false);
    } else if (strcmp(state->valuestring, "OFF") == 0) {
      ble_mesh_send_gen_onoff_set(0, addr);
      snprintf(state_payload, sizeof(state_payload), "{\"state\":\"OFF\"}");
      esp_mqtt_client_publish(mqtt_client, state_topic, state_payload, 0, 0,
                              false);
    }
  }

  cJSON_Delete(json);
}

static void mqtt_event_handler(void *handler_args, esp_event_base_t base,
                               int32_t event_id, void *event_data) {
  esp_mqtt_event_handle_t event = event_data;
  mqtt_client = event->client;

  switch ((esp_mqtt_event_id_t)event_id) {
  case MQTT_EVENT_CONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_CONNECTED");
    esp_mqtt_client_subscribe(mqtt_client, "homeassistant/status", 0);
    refresh_mqtt_subscriptions();
    break;
  case MQTT_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "MQTT_EVENT_DISCONNECTED");
    break;
  case MQTT_EVENT_DATA:
    ESP_LOGI(TAG, "MQTT_EVENT_DATA");
    if (strncmp(event->topic, "homeassistant/status", event->topic_len) == 0 &&
        strncmp(event->data, "online", event->data_len) == 0) {
      publish_ha_discovery_messages();
    } else if (strstr(event->topic, "/set") != NULL) {
      handle_lamp_command(event);
    }
    break;
  case MQTT_EVENT_ERROR:
    ESP_LOGE(TAG, "MQTT_EVENT_ERROR");
    if (event->error_handle->error_type == MQTT_ERROR_TYPE_TCP_TRANSPORT) {
      ESP_LOGE(TAG, "Last error reported from esp-tls: 0x%x",
               event->error_handle->esp_tls_last_esp_err);
    }
    break;
  default:
    break;
  }
}

static void mqtt_app_start(void) {
  // Initialize with defaults from SDKConfig
  strncpy(s_mqtt_url, CONFIG_BROKER_URL, sizeof(s_mqtt_url) - 1);
  strncpy(s_mqtt_user, CONFIG_USERNAME_MQTT, sizeof(s_mqtt_user) - 1);
  strncpy(s_mqtt_pass, CONFIG_PASSWORD_MQTT, sizeof(s_mqtt_pass) - 1);

  // Try to load overrides from NVS
  nvs_handle_t mqtt_nvs_handle;
  esp_err_t err = nvs_open("mqtt_config", NVS_READONLY, &mqtt_nvs_handle);
  if (err == ESP_OK) {
    size_t len;

    len = sizeof(s_mqtt_url);
    if (nvs_get_str(mqtt_nvs_handle, "broker_url", s_mqtt_url, &len) ==
        ESP_OK) {
      ESP_LOGI(TAG, "Loaded MQTT URL from NVS: %s", s_mqtt_url);
    }

    len = sizeof(s_mqtt_user);
    if (nvs_get_str(mqtt_nvs_handle, "username", s_mqtt_user, &len) == ESP_OK) {
      ESP_LOGI(TAG, "Loaded MQTT User from NVS");
    }

    len = sizeof(s_mqtt_pass);
    if (nvs_get_str(mqtt_nvs_handle, "password", s_mqtt_pass, &len) == ESP_OK) {
      ESP_LOGI(TAG, "Loaded MQTT Password from NVS");
    }

    nvs_close(mqtt_nvs_handle);
  } else {
    ESP_LOGW(TAG, "No NVS MQTT config found, using defaults");
  }

  esp_mqtt_client_config_t mqtt_cfg = {
      .broker.address.uri = s_mqtt_url,
      .credentials.username = s_mqtt_user,
      .credentials.authentication.password = s_mqtt_pass,
  };
  esp_mqtt_client_handle_t client = esp_mqtt_client_init(&mqtt_cfg);
  esp_mqtt_client_register_event(client, ESP_EVENT_ANY_ID, mqtt_event_handler,
                                 NULL);
  esp_mqtt_client_start(client);
}

/* --- Main Application Entry --- */

void app_main(void) {
  esp_err_t err;

  ESP_LOGI(TAG, "Initializing...");

  board_init();

  err = nvs_flash_init();
  if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
      err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
    ESP_ERROR_CHECK(nvs_flash_erase());
    err = nvs_flash_init();
  }
  ESP_ERROR_CHECK(err);

  // Initialize the lamp storage system from NVS
  lamp_nvs_init();

  // Ensure system events and netif are initialized (safe to call if already
  // inited)
  esp_netif_init();
  esp_event_loop_create_default();

  // Initialize Ethernet
  bool ethernet_enabled = false;
#ifdef CONFIG_ENABLE_ETHERNET
  if (ethernet_setup_init() == ESP_OK) {
    ethernet_enabled = true;
  }
#endif

  // --- WI-FI SETUP ---
  // Try to connect. If it fails, it will start the AP and return ESP_FAIL.
  if (wifi_setup_init() != ESP_OK) {
    if (ethernet_enabled) {
      ESP_LOGI(TAG, "Wi-Fi not connected (AP started), but Ethernet is "
                    "enabled. Proceeding with application.");
    } else {
      ESP_LOGW(TAG, "Wi-Fi connection failed or not configured.");
      ESP_LOGW(TAG, "Entering Setup Mode. Connect to Wi-Fi 'LEDVANCE_Setup' "
                    "and visit 192.168.4.1");
      // We return here to stop the main application (MQTT, Mesh, etc.) from
      // running while the user is configuring the device.
      return;
    }
  }

  // Initialize Bluetooth and BLE Mesh
  err = bluetooth_init();
  if (err) {
    ESP_LOGE(TAG, "bluetooth_init failed (err %d)", err);
    return;
  }

  err = ble_mesh_nvs_open(&NVS_HANDLE);
  if (err) {
    ESP_LOGE(TAG, "ble_mesh_nvs_open failed (err %d)", err);
    return;
  }

  ble_mesh_get_dev_uuid(dev_uuid);

  err = ble_mesh_init();
  if (err) {
    ESP_LOGE(TAG, "ble_mesh_init failed (err %d)", err);
    return;
  }

  // Start MQTT client
  mqtt_app_start();

  // Start HTTP server (for configuration, etc.)
  start_webserver();

  ESP_LOGI(TAG, "Initialization complete. Gateway is running.");
}
