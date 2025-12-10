#include "ethernet_setup.h"
#include "driver/gpio.h"
#include "esp_eth.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include <string.h>

#ifdef CONFIG_ENABLE_ETHERNET

static const char *TAG = "ETH_SETUP";

/** Event handler for Ethernet events */
static void eth_event_handler(void *arg, esp_event_base_t event_base,
                              int32_t event_id, void *event_data) {
  uint8_t mac_addr[6] = {0};
  /* we can get the ethernet driver handle from event data */
  esp_eth_handle_t eth_handle = *(esp_eth_handle_t *)event_data;

  switch (event_id) {
  case ETH_EVENT_CONNECTED:
    esp_eth_ioctl(eth_handle, ETH_CMD_G_MAC_ADDR, mac_addr);
    ESP_LOGI(TAG, "Ethernet Link Up");
    ESP_LOGI(TAG, "Ethernet HW Addr %02x:%02x:%02x:%02x:%02x:%02x", mac_addr[0],
             mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
    break;
  case ETH_EVENT_DISCONNECTED:
    ESP_LOGI(TAG, "Ethernet Link Down");
    break;
  case ETH_EVENT_START:
    ESP_LOGI(TAG, "Ethernet Started");
    break;
  case ETH_EVENT_STOP:
    ESP_LOGI(TAG, "Ethernet Stopped");
    break;
  default:
    break;
  }
}

/** Event handler for IP_EVENT_ETH_GOT_IP */
static void got_ip_event_handler(void *arg, esp_event_base_t event_base,
                                 int32_t event_id, void *event_data) {
  ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
  const esp_netif_ip_info_t *ip_info = &event->ip_info;

  ESP_LOGI(TAG, "Ethernet Got IP Address");
  ESP_LOGI(TAG, "~~~~~~~~~~~");
  ESP_LOGI(TAG, "ETHIP:" IPSTR, IP2STR(&ip_info->ip));
  ESP_LOGI(TAG, "ETHMASK:" IPSTR, IP2STR(&ip_info->netmask));
  ESP_LOGI(TAG, "ETHGW:" IPSTR, IP2STR(&ip_info->gw));
  ESP_LOGI(TAG, "~~~~~~~~~~~");
}

esp_err_t ethernet_setup_init(void) {
  // 1. Load Configuration from NVS
  nvs_handle_t nvs_handle;
  esp_err_t err = nvs_open("eth_config", NVS_READONLY, &nvs_handle);

  int32_t phy_type = CONFIG_ETH_PHY_DEFAULT_TYPE;
  int32_t mdc_gpio = CONFIG_ETH_MDC_GPIO;
  int32_t mdio_gpio = CONFIG_ETH_MDIO_GPIO;
  int32_t phy_addr = CONFIG_ETH_PHY_ADDR;
  int32_t rst_gpio = CONFIG_ETH_PHY_RST_GPIO;
  int32_t pwr_gpio = CONFIG_ETH_PHY_POWER_GPIO;
  int32_t clk_mode =
      0; // 0: Input(GPIO0), 1: Output-GPIO0, 2: Output-GPIO16, 3: Output-GPIO17
  uint8_t eth_enabled_nvs = 1;

  if (err == ESP_OK) {
    ESP_LOGI(TAG, "Loading Ethernet config from NVS...");
    nvs_get_u8(nvs_handle, "enable", &eth_enabled_nvs);
    if (eth_enabled_nvs == 0) {
      ESP_LOGI(TAG, "Ethernet disabled in NVS.");
      nvs_close(nvs_handle);
      return ESP_ERR_NOT_SUPPORTED; // Signal that it's disabled
    }

    nvs_get_i32(nvs_handle, "phy_type", &phy_type);
    nvs_get_i32(nvs_handle, "mdc", &mdc_gpio);
    nvs_get_i32(nvs_handle, "mdio", &mdio_gpio);
    nvs_get_i32(nvs_handle, "addr", &phy_addr);
    nvs_get_i32(nvs_handle, "rst", &rst_gpio);
    nvs_get_i32(nvs_handle, "pwr", &pwr_gpio); // Optional
    nvs_get_i32(nvs_handle, "clk_mode", &clk_mode);
    nvs_close(nvs_handle);
  } else {
    // Fallback to Kconfig defaults
    ESP_LOGI(TAG, "No NVS config found. Using Kconfig defaults.");
    // If you want to FORCE setup mode for generic binary, maybe default to
    // disabled if not in NVS? But for backward compatibility or ease of dev, we
    // keep Kconfig defaults.
  }

  ESP_LOGI(TAG,
           "ETH Config: Type=%d, MDC=%d, MDIO=%d, Addr=%d, Rst=%d, ClkMode=%d",
           phy_type, mdc_gpio, mdio_gpio, phy_addr, rst_gpio, clk_mode);

  esp_netif_config_t cfg = ESP_NETIF_DEFAULT_ETH();
  esp_netif_t *eth_netif = esp_netif_new(&cfg);
  if (eth_netif == NULL) {
    ESP_LOGE(TAG, "Failed to create esp_netif for Ethernet");
    return ESP_FAIL;
  }

  // Init MAC and PHY configs to default
  eth_mac_config_t mac_config = ETH_MAC_DEFAULT_CONFIG();
  eth_phy_config_t phy_config = ETH_PHY_DEFAULT_CONFIG();

  phy_config.phy_addr = phy_addr;
  phy_config.reset_gpio_num = rst_gpio;

  esp_eth_phy_t *phy = NULL;
  switch (phy_type) {
  case 0:
    phy = esp_eth_phy_new_lan87xx(&phy_config);
    break;
  case 1:
    phy = esp_eth_phy_new_ip101(&phy_config);
    break;
  case 2:
    phy = esp_eth_phy_new_rtl8201(&phy_config);
    break;
  case 3:
    phy = esp_eth_phy_new_dp83848(&phy_config);
    break;
  default:
    ESP_LOGE(TAG, "Unknown PHY Type %d", phy_type);
    return ESP_FAIL;
  }

  if (phy == NULL) {
    ESP_LOGE(TAG, "Failed to create PHY instance");
    return ESP_FAIL;
  }

  eth_esp32_emac_config_t esp32_emac_config = ETH_ESP32_EMAC_DEFAULT_CONFIG();
  esp32_emac_config.smi_gpio.mdc_num = mdc_gpio;
  esp32_emac_config.smi_gpio.mdio_num = mdio_gpio;

  // Config Clock
  if (clk_mode == 0) {
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_EXT_IN;
    esp32_emac_config.clock_config.rmii.clock_gpio = EMAC_CLK_IN_GPIO; // GPIO0
  } else if (clk_mode == 2) {
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
    esp32_emac_config.clock_config.rmii.clock_gpio = 16;
  } else if (clk_mode == 3) {
    esp32_emac_config.clock_config.rmii.clock_mode = EMAC_CLK_OUT;
    esp32_emac_config.clock_config.rmii.clock_gpio = 17;
  }
  // clk_mode 1 (Output GPIO0) is technically possible on some silicon but
  // usually external. Ignore for now.

  esp_eth_mac_t *mac = esp_eth_mac_new_esp32(&esp32_emac_config, &mac_config);

  esp_eth_config_t config = ETH_DEFAULT_CONFIG(mac, phy);
  esp_eth_handle_t eth_handle = NULL;
  ESP_ERROR_CHECK(esp_eth_driver_install(&config, &eth_handle));

  /* attach Ethernet driver to TCP/IP stack */
  ESP_ERROR_CHECK(
      esp_netif_attach(eth_netif, esp_eth_new_netif_glue(eth_handle)));

  // Register user defined event handers
  ESP_ERROR_CHECK(esp_event_handler_register(ETH_EVENT, ESP_EVENT_ANY_ID,
                                             &eth_event_handler, NULL));
  ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_ETH_GOT_IP,
                                             &got_ip_event_handler, NULL));

  /* start Ethernet driver state machine */
  ESP_ERROR_CHECK(esp_eth_start(eth_handle));

  return ESP_OK;
}

#else // CONFIG_ENABLE_ETHERNET

esp_err_t ethernet_setup_init(void) {
  // Ethernet disabled
  return ESP_OK;
}

#endif
