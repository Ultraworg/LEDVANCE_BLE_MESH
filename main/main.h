#ifndef MAIN_H
#define MAIN_H

/**
 * @brief Subscribes/re-subscribes to the command topics for all configured lamps.
 */
void refresh_mqtt_subscriptions(void);

/**
 * @brief Publishes Home Assistant discovery messages for all configured lamps.
 */
void publish_ha_discovery_messages(void);

#endif /* MAIN_H */