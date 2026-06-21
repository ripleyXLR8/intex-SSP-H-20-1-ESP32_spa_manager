# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## What this is

Firmware for an ESP32 (DevKitC) that replaces the original motherboard of an Intex SSP-H-20-1 spa. It reads the spa's sensors, drives its relays (heaters, jet/air pump, water pump), and exposes control over WiFi/MQTT for home automation. The entire firmware is a single Arduino sketch: `intex_spa_manager.ino`. There is no separate build system — it is compiled and flashed from the Arduino IDE.

⚠️ This controls 220-240 VAC mains hardware in a wet environment. Logic that drives `HEATER_*_PIN`, `JET_PIN`, or `PUMP_PIN`, and the safety interlocks around them, is safety-critical. See README warnings before touching anything that changes relay behavior.

## Build / flash / debug

- **Compile & upload:** Arduino IDE, board = ESP32 Dev Module, baud 115200. No CLI build is configured in this repo.
- **Required libraries (Arduino Library Manager):** `PubSubClient`, `Arduino_JSON`, `WiFiManager` (tzapu), plus the ESP32 core bundles `WiFi`, `ArduinoOTA`, `esp_task_wdt`, `Preferences`.
- **OTA updates:** after the first USB flash, the device registers with ArduinoOTA, so subsequent uploads can be done over WiFi (no need to open the enclosure).
- **Serial/Telnet debug:** logs go to `Serial` (115200) AND to a Telnet server on port 23 (`telnet <device-ip>`). Use `debugPrint`/`debugPrintln` (not raw `Serial.print`) so output reaches both. Output uses ANSI color codes (the `C_*` macros).
- **No test suite** exists; verification is done on hardware via the serial/Telnet dashboard printed every 5 s in `loop()`.

## First-boot configuration (no hardcoded credentials)

There are no WiFi/MQTT constants to edit. On boot the device runs **WiFiManager**: if it can't connect it opens a captive-portal AP named `Spa-Intex-Config` where WiFi + MQTT server/port/user/pass are entered. MQTT settings persist in NVS via `Preferences` (namespace `spa_config`). Holding **GPIO 0 (BOOT button)** LOW at startup wipes saved WiFi (`wm.resetSettings()`). If WiFi fails, the device enters `offline_mode` and still runs the local heating/relay logic without network. (The README's older instructions about editing `ssid`/`password`/`mqttServer` constants and the aREST HTTP API are stale — the current firmware uses WiFiManager + MQTT only.)

## Runtime architecture

Classic Arduino `setup()` / non-blocking `loop()`. `loop()` is gated by `millis()`-based timers (no `delay()` in the main path) so the watchdog stays fed:
- `read_sensors()` every 500 ms — reads thermistors and (currently stubbed) flow sensors.
- `update_relay_state()` every loop — the actuator authority. Calls `activate_jet`, `activate_filtration`, and `activate_heating(must_heat())`.
- `update_mqtt_server()` every 20 s (or when `request_mqtt_update` is set) — publishes full state JSON to `spa_intex/info`.
- An **esp_task_wdt** (8 s) reboots the board if the loop stalls; `esp_task_wdt_reset()` is called at the end of every loop, and OTA start removes the WDT so flashing doesn't trip it. `connect_mqtt()` sets `MQTTclient.setSocketTimeout(4)` so a blocking connect to an unreachable broker stays under the WDT — keep any blocking call in the loop path under 8 s.

State lives in module-level globals (`current_temp`, `target_temp`, `heating_enabled`, `temp_regulation_enabled`, `filtration_enabled`, `jet_enabled`, `flow_1/2`, etc.). MQTT callbacks set these; `update_relay_state()` acts on them. Keep that direction — commands mutate state flags, the loop translates flags to pins.

### Heating control & safety interlocks (`must_heat()`)
This is the core safety logic. Heating is only allowed when ALL hold: `temp_regulation_enabled`, `flow_1` AND `flow_2` (water flow present on both sensors, unless overridden by `bypass_flow`), `fuse` (thermal fuse intact, unless overridden by `bypass_fuse`), and `current_temp < max_temp` (39 °C hard cap — never bypassable). The same `(flow_1 || bypass_flow) && (flow_2 || bypass_flow)` / `(fuse || bypass_fuse)` gate is duplicated in two places: `must_heat()` and `activate_heating()` — keep both in sync. Within that, a **hysteresis** band of ±1.0 °C around `target_temp` prevents chattering, and an **anti-short-cycle** lock (`minHeatingCycle` = 15 min) blocks state changes until the minimum cycle elapses — except `isFirstCycle`, which bypasses the lock once on startup. `lastHeatingStateChange` tracks the timer. When changing this function, preserve the flow + fuse + max-temp + hysteresis + anti-cycle guarantees.

Inrush limiting (in `activate_heating()`): heater 2 energizes only ~20 s after heater 1 (`heaterStageDelay`) and never while the jet runs — keep both rules.

### Sensors
- **Temperature:** NTC thermistors on `TEMP_1_PIN`/`TEMP_2_PIN` (ADC), oversampled (`num_samples`) and EMA-smoothed (`smoothing_factor`) because the ESP32 ADC is noisy. Resistance→temp uses the logarithmic fit `T = -7750*ln(R) + 28589` in `read_temperature()` (see README for the calibration derivation). `temp_2` is used as `current_temp`.
- **Flow / fuse:** `read_sensors()` reads the real digital inputs into `flow_1`/`flow_2`/`fuse`. The active levels are `#define`d (`FLOW_ACTIVE_LEVEL`, `FUSE_OK_LEVEL`, both `HIGH`) — verify against the board; a disconnected input fails to the blocking state. Either interlock can be temporarily overridden via the `bypass_flow`/`bypass_fuse` MQTT switches (see MQTT topics).

### MQTT topics
Command/state topics are under `spa_intex/` (e.g. `spa_intex/target`, `/thermostat`, `/filtration`, `/jet`, `/heater`, `/reset`; state published to `/info`). On connect, `publishMQTTDiscovery()` publishes Home Assistant MQTT auto-discovery configs under `homeassistant/...`. `spa_intex/reset` triggers a deferred (10 s) reboot via the non-blocking `resetRequested`/`resetTimer` mechanism in `loop()`.

`spa_intex/bypass_flow` and `spa_intex/bypass_fuse` (`1`/`0`) set the `bypass_flow`/`bypass_fuse` globals that override the flow and thermal-fuse interlocks. They are **non-persistent by design** (default `false`, reset to OFF on every boot — no `Preferences` involvement), published in `/info`, and exposed as `ent_cat:config` HA switches. To add a new command topic, follow the existing pattern in five places: declare the `const char*`, `subscribe()` in `connect_mqtt()`, add a branch to `mqttcallback()`, publish it in `update_mqtt_server()`, and add a discovery entry in `publishMQTTDiscovery()`.

## Pin map (set in `#define`s near the top)
Heaters 18/19, Jet 5, Pump 17, Flow 23/22, Temp ADC 34/35, Temp-fuse 32. Eagle schematic/board files are in `eagle_files/`; annotated reverse-engineering photos and the temp-resistance curve are in `images/`.

## Conventions
- Comments and serial/log strings are in **English** (the sketch was fully translated from French); keep new strings English. Note: MQTT topic strings, JSON keys, and HA `uniq_id`s are protocol identifiers — never translate those.
- Use the `debugPrint*` helpers and `C_*` ANSI macros for any new logging. In hot paths (e.g. the 5 s dashboard) build lines with `snprintf` into stack `char` buffers and the `fmtState()` helper rather than Arduino `String` concatenation, to avoid heap churn/fragmentation; `C_*` macros are string literals so they concatenate into format strings at compile time.
- Keep `loop()` non-blocking — avoid `delay()` in the main path (short `delay()`s exist only in `setup()`/relay test/heater staging) so the watchdog isn't tripped.
