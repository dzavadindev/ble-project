# StairLight

StairLight is an interactive staircase light that follows a person as they walk up or down the stairs. A radar sensor detects their position, and an addressable LED strip lights the path with a moving effect and start/end animations.

## How it works

Two ESP32-based Heltec boards communicate over Bluetooth Low Energy (BLE). The radar node reads position data from an RD-03D mmWave sensor and sends it to the LED controller. The controller maps distance along the staircase to LEDs and drives the strip using FastLED and the project's `StairChaser` code. A React web app connects over BLE to set color and brightness and display the active LED range.

## Hardware

- Two Heltec ESP32 boards (sketches configured for **Heltec Wireless Tracker**)
- Ai-Thinker RD-03D mmWave radar module
- 5 m WS2812B addressable LED strip (5 V, 60 LEDs/m)
- 5 V power supply for the strip and controller (the project used a 50 W supply)

## Run it

1. Install the LED strip on the staircase and connect it to the LED controller board and a suitable 5 V supply. Connect the radar module to the second board. Check the pin definitions in the sketches (`led-controller/led-controller.ino` and `client-node-mmwave/client-node-mmwave.ino`) against your wiring.
2. Using the Arduino toolchain with Heltec ESP32 board support and FastLED installed, upload `led-controller/led-controller.ino` to the controller board and `client-node-mmwave/client-node-mmwave.ino` to the radar board. Power both boards; the radar node will find and connect to the controller automatically.
3. To use the optional web controller, run:

   ```sh
   cd web-control-ble
   npm install
   npm run dev
   ```

   Open the local URL printed by Vite in a browser with Web Bluetooth support, then select **StairLight** using the connect button. Walk along the stairs to see the lights follow you.

## Collaborators

Thijs Besseling and Danill Zavadin.
