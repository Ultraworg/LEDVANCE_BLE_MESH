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
3. In the Elements of the App, assign the same Application Key to the following Elements:
(Not sure if all of them are really required but this is how it works for me)
   1. Generic OnOff Server
   2. Generic Level Server
   3. Generic Power OnOff Server
   4. Light Lightness Server
4. The lamp will now have an Unicast Address -> take a note of this
5. Repeat the steps above for all your lamps
6. Clone the repository
7. Adopt the sdkconfig example with your WIFI password and the Home Assistant credentials (create a new User in HA to use with MQTT) & rename the file to sdkconfig
8. Build and flash the project to your ESP
9. Open the IP address of your ESP and add your lights on the homepage. Name & Unicast Address (from Step 4) are required (whenever new lamps are  added, the ESP will require a restart so that the MQTT subscribes also to the newly added lights)
10. Restart the ESP32 to load the new config at startup
11. Provision the ESP -> same as Steps 1-3, with the following Elements:
    1.  Generic OnOff Client
    2.  Generic Level Client
    3.  Light Lightness Client



Example config message:

Topic: `homeassistant/light/test/config`


Payload:
```
{
   "name":null,
   "~":"homeassistant/light/test",
   "cmd_t":"~/set",
   "stat_t":"~/state",
   "schema": "json",
   "brightness": true,
   "bri_scl":50,
   "pl_on": "ON",
   "pl_off": "OFF",
   "uniq_id":"lamp04",
   "dev":{
      "ids":[
         "lamp04"
      ],
      "name":"Lamp Office"
   }
}
```

Once the entity appears in Home Assistant, it should work.