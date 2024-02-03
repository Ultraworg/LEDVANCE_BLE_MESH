LEDVANCE ESP BLE Mesh Client Model
========================

This code can be used to program an ESP board to act as an intermediate between Home Assistant and LEDVANCE Bluetooth bulbs.
It supports On/Off and also setting the brightness level.

It is based on the On-Off client example from the [ESP idf examples](https://github.com/espressif/esp-idf/tree/a5b261f/examples/bluetooth/esp_ble_mesh/onoff_models/onoff_server)

The code is not clean or anything but it works and might help others to write better code.

To install the code on the ESP follow the steps below:

What you need:
- nRF Mesh App (https://www.nordicsemi.com/Products/Development-tools/nrf-mesh/getstarted)
-> To provision the lamps and the ESP
- ESP board
- ESP IDE ->
If not already done, follow the Getting Started guide to install the ESP IDE (I used the VSCode Extension)
https://docs.espressif.com/projects/esp-idf/en/latest/esp32/get-started/index.html

## Instructions:
1. Provision the lamps with the nRF Connect App (they need to be in pairing mode to be provisioned)
2. In the App, assign it an Application Key
3. In the Elements of the App, assign an Application Key to the following Elements:
(Not sure if all of them are really required but this is how it works for me)
   1. Generic OnOff Server
   2. Generic Level Server
   3. Generic Power OnOff Server
   4. Light Lightness Server
4. The lamp will now have an Unicast Address -> take a note of this
5. Repeat the steps above for all your lamps
6. Clone the repository
7. Adopt the sdkconfig with your WIFI password and the Home Assistant credentials (create a new User in HA to use with MQTT)
8. Adopt the `mqtt_event_handler` function in the `main.c` file to the Unicast Addresses of your lamps
9.  Build and flash the project to your ESP
10. Provision the ESP -> same as Steps 1-3, with the following Elements:
    1.  Generic OnOff Client
    2.  Generic Level Client
    3.  Light Lightness Client

I have not yet wrote the code to create the entities in Home Assistant so for now the config message has to be sent manually with any MQTT client.
I used [MQTTX](https://mqttx.app/)

Example config message:

Topic: `homeassistant/light/living/config`


Payload:
```
{
    "name":"LivingRoomLamp",
    "command_topic":"homeassistant/switch/living/set",
    "state_topic":"homeassistant/switch/living/state",
    "unique_id":"lamp01",
    "device":{
        "identifiers":[
         "lamp01"
      ],
    "name":"Lamp_Living_Room"
    }
}
```

Once the entity appears in Home Assistant, it should work.