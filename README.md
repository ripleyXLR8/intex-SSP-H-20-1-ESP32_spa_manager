# WORK IN PROGRESS

# Intex SSP-H-20-1 ESP32 Spa Manager

**WARNING 1 : The circuit in this project uses main voltage (220-240 Volt AC in western Europe). This can be deadly if not handled properly! You can easily hurt yourself as well. Build this circuit at your own risk.**

**WARNING 2 : Never ever ever power this circuit directly without a GFCI outlet (differential circuit breaker). Remember this wircuit will be used in a wet environnement. If you don't understand this, please watch this video https://www.youtube.com/watch?v=SHGo-52wCDc&t=63s and get some electrical knowledge about ground protection.**

**WARNING 3 : This project is still a work in progress, this page is a kind of building log, I haven't finish the board building and installation for the moment. Watch the issues page to see what is working and what is not.**

This project aims to build a replacement for the motherboard of the Intex SSP-20 Spa (it may work with other Intex spa and could be used to build a spa from scratch, but this is not the purpose of this page). My goal was to be able to control the spa remotely and integrate it in my domotic system. The board communicates over the MQTT protocol and publishes Home Assistant MQTT auto-discovery messages, so it integrates out of the box with Home Assistant (and works with any MQTT-based system such as Jeedom https://www.jeedom.com/). The second goal of this project is to improve the reliability of this spa and specificaly to solve a random E95 or E9X error problem. My spa was still under waranty when i started this project so I tried to be as stealth as possible.

## 1) Intex motherboard reverse engineering
Below the annotate picture of the motherboard with my understanding of its working principle. Since the card is labeled, everything is pretty straight forward except the descaler system and the pump system (the upper terminal barrier connector on the board).

![Motherboard schematic](/images/motherboard_schematic.png)

### a) Sensors
There are 5 sensors connected to the board, 2 temperature sensors (black and white connectors), 2 flow sensors (red and green connectors) and 1 temperature fuse (yellow connector). They are all connected with a 3 pins JST like connector (it looks like a JST XH 3P but the JST XH 3P doesn't have a lock mecanism).

#### Temperature sensors
Both temperature sensors are negative temperature coefficient (NTC) thermistore, it means that the resistance of the thermistore decreases with an increase in temperature.

- The temperature sensor 1 is installed at the intake of the water pump so it reads the water temperature and it is use to regulate the water temperature from 20°C to 40°C.

- The temperature sensor 2 is installed in the core of  heating elements and its function is to detect overheating over 50°C and shut down the heating if necessary.

Since I'm reading a resistance value and not a temperature, I need to do some calibration (i.e. find a relation between the resistance I'm measuring and the temperature). To measure the real temperature of the water in the spa I connected a DS18B20 thermal sensor in place of the LCD panel. Then I have recorded the temperature of the water and the resistance of both integrated temperature probe of the spa during the heating process of the spa from 18°C to 40°C. The resut once smoothed is the curve presented below. (smoothing is mandatory since the ADC of the ESP32 is pretty noisy...)

![Temperature resistance curve](/images/temperature-resistance-curve.jpg)

I first tried to use the Steinhart-Hart equation https://fr.wikipedia.org/wiki/Relation_de_Steinhart-Hart but O did not get good results using this equation, especialy from 34 to 38 degrees.

Since I'm observing that the curve is pretty linear from 18 to 38 degrees, and that the value of the resistance is 0 for 40°C, I tried a linear regression. The result is pretty decent with a R=0,9926.

The equation is T = -276,38*R + 10513

You can also try a logarithmic regression an get the following equation : T = -7750*Ln(R) + 28589 

#### Temperature fuse
One temperature fuse is installed just below heating elements. I cannot test the triggering temperature but it seems that in normal condition the fuse let the current pass and if the temperature goes over 84°C then the fuse burn and the current stop passing and shut down the heating.

#### Flow sensors
Both flow sensors let the current pass only if flow is detected, they ensure that no heating is activated if there is no flow detected in the system.

### b) Power supply, relays and other parts
All the power cable (input and outputs) are connected by a barrier terminal block. We will use the same connector on our board because we don't want to alter the power cable and change their connector.

#### Heating elements
There is two heating element in the spa for a total power of 2000 Watts. They are directly powered by a relay allowing 220VAC to pass trough the heating element.
Please note that in normal condition :
- The water pump is always powered when heating. Any flow interruption signaled by the flow sensor result in an error and the heating stops.
- When you push the heating button the heating doesn't start immediately, the water pump starts and then a couple seconds after the heater 1 is powered then a couple seconds later the heater 2 starts.
- The heater 2 is shut-down if the jet pump is activated.

#### Jet pump
The jet pump is directly powered by a relay allowing 220VAC to pass trough the pump and pumping air into the Spa.

#### Water pump
The water pump is powered by 12VAC but before that a relay allows 220VAC to flow through a dual output 12VAC transformer (see the drawing above). The first output is send to a closed relay and the second output of the transformer goes into a full bridge rectifier producing 12VDC send to activate the relay and let the 12VAC flow into the water pump. It seems that the only goal of this system is to allow the descaling system to start at the same time as the pump (see below the descaling system). On this version on the board we will not implement the descaling system (maybe on the next version) so we will power the pump directly from the output transformer. (connection of A and B wires to C and D wires)

#### Control panel
I will replace the control panel with an external control box connected through the enclosure of the spa with an IP68 SP21-12 connector. (available here : https://www.amazon.com/gp/product/B071R6RSN6/ref=ppx_yo_dt_b_asin_title_o00_s00?ie=UTF8&psc=1)

#### Descaler system
It seems that this spa is equiped by a magnetic descaling system. Here is the wikipedia page on this technology (https://en.wikipedia.org/wiki/Magnetic_water_treatment). I cannot find the type of signal send to this device, it seems that the signal is AC with a modulation of amplitude. I will maybe investigate it later... so this function has not been implemented on the replacement board.

## 2) Designing the replacement board
### a) Working principle
An ESP32 (DevKitc) is the heart of the system. It allows us to connect : 
- 2 temperature sensors on 2 analog inputs,
- 2 flow sensors on 2 digital inputs,
- 1 temperature fuse on 1 digital input,
- 5 buttons on 5 digital input (PUMP, HEATING, JET, +, -),
- 1 LCD display on 2 digital inputs (I2C protocol),
- 4 power relays on 4 digital inputs.

Relays, LCD, temperature sensors, flow sensors, and buttons will not be powered from the 5 Volt pin of the arduino, we will use directly the 5 Volt DC power supply to power them directly.

The integrated wifi chip also allow us to connect to the wifi and the MQTT server to send and receive data.

### b) Electrical drawing
I used Eagle 8.2.2 to design the replacement board, below is the electrical drawing. The eagle files will be available soon.

![Electrical schematic](/images/electrical_schematic.png)

### c) Board schematic
I wanted the board to be easily produce at home by anybody so I choose to use a single side PCB with plated-trhough holes (PTH) components. SMD component will be a also good choice, but I know that occasionnal welder are more at ease with PTH components. The drilling size is 0.8mm, the width of the electrical tracks is 16mil (0.40mm) and the clearance is 10mil (0.25mm), theses parameters allows you to produce the board using the toner transfer method (this method requires very few tools).

Relays are integrated on the board to be integrated more easily in the original enclosure.

The original motherboard dimension are 10.5 cm x 17 cm, mounting hole are 5mm wide and are placed 4.5mm from the edge of the board. The PCB will be mounted using the same mounting screw used by the original motherboard (but I don't think I will be using the center plastic lock).

The power tracks (tracks going to relays) will carry a lot of current. The total power of the Spa announced by Intex is 2200 watts. I have made some measurement on the original board to see how much current is use by each component :

- Heater 1 : 6.70A (Peak at startup) / 3.70A (Cruise) @ 220 Volt AC
- Heater 2 : 7.00A (Peak at startup) / 4.00A (Cruise) @ 220 Volt AC
- Water pump : 0.16A @ 12 Volt AC (This measure include also de descaler) 
- Air pump : 5.50A (Peak at startup) / 3.60A (Cruise) @ 220 Volt AC

As we can see, if everything is running at the same time the spa can draw a total of 11.46A with a peak at startup of 19,36A which is a lot. In the original board there is some rules to avoid the spa to reach this high current drawing :

- Heater 1 and 2 have a 20 seconds startup delay between each other.
- Air pump and heater 2 can't run at the same time.

We will implement the same rules to our board so the maximum current draw will be around 8A with a peak around 11A when starting the heater or the pump. (Which is about what is announced by Intex around 2400 Watts). By dimension the track width for 10A, 2oz/ft copper, an ambiant temperature of 25°C and a maximum temperature elevation of 5 degrees, we find that we need a 2,36 mm (93mil) track width.

### d) Board production
Board schematic has been sent to PCBway a chinese PCB prototyping company. https://www.pcbway.com/. They can even populate your board with the component if you want them to do it.

The list of component with links to buy them is provided below :

## 5) How to configure and install the new control board
### a) Uploading the code and first-boot configuration
Open the lastest `*.ino` file within the Arduino IDE. You no longer need to edit WiFi or MQTT credentials in the code — they are configured at runtime through a captive WiFi portal (WiFiManager) and saved in the ESP32 flash memory.

The feature flags at the top of the sketch are enabled by default and normally don't need to be changed :
`const bool wifi_enable = true;`
`const bool mqtt_enable = true;`
`const bool ota_enable = true;`  // WiFi OTA: lets you upload future updates over the air, without opening the case

Place your ESP32 on a breadboard, select the ESP32 board, set the baudrate to 115200 and upload the code. Once the upload is finished, press the reset button of the board.

On first boot (or whenever it cannot reach a known WiFi network) the board starts a WiFi access point named **`Spa-Intex-Config`**. Connect to it with a phone or computer and a captive portal opens where you set :
- your WiFi network and password,
- the MQTT server address and port (default `1883`),
- the MQTT username and password (leave empty if your broker has no authentication),
- the **OTA password** that protects over-the-air firmware updates.

These settings are stored permanently in the ESP32 flash. To erase the saved WiFi configuration and force the portal to reopen, **hold the BOOT button (GPIO 0)** while resetting the board.

⚠️ **Set the OTA password.** If you leave it empty, anyone on your network can flash arbitrary firmware to a board that switches 2 kW of mains heaters. The board logs a red warning at start-up while the OTA password is unset.

If the board cannot connect to WiFi within the portal timeout (180 s) it falls back to an **offline mode** : the local heating, filtration and jet logic keeps running, but no remote control is available.

### b) Checking the board connection
Open the serial port monitor of the Arduino IDE and set the baudrate to 115200. You should see the start-up sequence and the IP of your board on the WiFi network. Once connected, you can also open a **Telnet** session to that IP (port 23) to watch the live dashboard remotely — it prints the temperature, target, thermostat/filtration/jet/heater states and the anti-short-cycle timer every few seconds.

If you have any error message during the start-up you should check your configuration in the WiFi portal.

### c) Controlling the spa over MQTT
The board uses MQTT only (the previous HTTP/REST interface has been removed). It publishes its full state as a JSON payload to the **`spa_intex/info`** topic, for example :

`{"flow_1":true,"flow_2":true,"temp_1":21.3,"temp_2":21.8,"target":35.0,"heartbeat":65065,"thermostat":false,"filtration":false,"heater":false,"jet":true,"fuse":true,"bypass_flow":false,"bypass_fuse":false}`

In that payload `fuse:true` means the thermal fuse reads intact (safe to heat) and `flow_1/flow_2:true` mean water flow is detected.

Command topics (publish `1` or `0`, or a number for the target) :
- `spa_intex/target` -> set the target temperature (°C, capped at 39 °C).
- `spa_intex/thermostat` -> enable/disable the temperature regulation (enabling it also turns filtration on).
- `spa_intex/filtration` -> enable/disable the filtration (water pump).
- `spa_intex/jet` -> enable/disable the jet (air pump / bubbles).
- `spa_intex/reset` -> publish `1` to reboot the board after a 10 s delay.
- `spa_intex/bypass_flow` -> override the water-flow interlock (see safety section below).
- `spa_intex/bypass_fuse` -> override the thermal-fuse interlock (see safety section below).

On every successful MQTT connection the board also publishes **Home Assistant MQTT auto-discovery** messages (under the `homeassistant/...` topics), so the temperature sensor, target setpoint, thermostat, filtration and jet appear automatically as a "Spa Intex" device in Home Assistant — no manual entity configuration required.

### d) Safety interlocks and current limiting
The heating logic enforces several hardware-protection rules. The heater stays off unless **all** of these hold:
- the thermostat is enabled and the water is below target (with a ±1 °C hysteresis band),
- **water flow is detected** on both flow sensors (`flow_1` and `flow_2`),
- the **thermal fuse reads intact** (`fuse`),
- the water temperature is below the 39 °C safety ceiling,
- the 15-minute anti-short-cycle timer allows the change (bypassed once on the first cycle).

To limit the inrush current (the original board draws up to ~19 A peak with everything starting at once), the firmware reproduces the factory rules: **heater 2 starts only ~20 s after heater 1**, and **heater 2 never runs at the same time as the jet/air pump** (turning the jet on temporarily drops heater 2). The live dashboard (serial / Telnet) shows which interlock is currently blocking the heater.

**Input polarity must match your board.** The active levels are defined at the top of the sketch:
`#define FLOW_ACTIVE_LEVEL HIGH` (level read when water flows) and `#define FUSE_OK_LEVEL HIGH` (level read when the fuse is intact). They default to *high = active*; if your wiring is inverted, flip these two `#define`s, otherwise the interlocks will read backwards. A disconnected flow or fuse input is treated as "no flow / fuse tripped" and **blocks heating** (fail-safe) — so the thermal-fuse (yellow) and flow connectors must be wired and working for the spa to heat.

#### Bypassing a broken sensor
If a flow sensor or the thermal fuse is faulty and you knowingly want to keep heating until you can replace it, two MQTT overrides let you bypass the interlock without touching the code:
- `spa_intex/bypass_flow` -> publish `1` to ignore the water-flow check, `0` to restore it.
- `spa_intex/bypass_fuse` -> publish `1` to ignore the thermal-fuse check, `0` to restore it.

Both are exposed as switches in the **Configuration** section of the Home Assistant device ("Spa Bypass Debit" / "Spa Bypass Fusible"), reported in the `info` payload (`bypass_flow` / `bypass_fuse`), and shown on the serial/Telnet dashboard (`Securites : Fusible [ACTIVE/CONTOURNEE] | Debit [...]`) with a red warning when engaged.

⚠️ **A bypass resets to OFF on every reboot/power-cycle** — it is deliberately not persisted, so cycling power always restores full safety. Bypassing the thermal fuse removes overheat protection; the only remaining backstop is the 39 °C software ceiling on the regulation sensor. Use these overrides only as a temporary measure and never leave the spa unattended while a bypass is active.

### e) Removing the old control board
First, it is very important to DISCONNECT THE POWER CABLE of the Spa. Remove the four screws securing the spa cover and remove it. Then locate the cover of the control board, remove all the screws and remove the cover. On the control board remove all cable from the lower and upper terminal, from the sensors and from the control panel, then remove the four screws securing the board and remove it.

### f) Installation of the new control board
Remove the central plastic pin in order to install the new board into the enclosure and then install the new board using the 4 screws from the old board. Then connect the old cables on the new board. Here is a quick connection guide based on the color of my board (double check it with you board before powering up the spa) :

- Red connector -> Flow 1 connector
- Green connector -> Flow 2 connector
- White connector ->  Temperature 2 connector
- Black connector ->  Temperature 1 connector
- Yellow connector -> DO NOT CONNECT (it doesn't work currently)
- Connector 1 -> N terminal
- Connector 2 -> L terminal
- Connector 5 -> N of the pump terminal
- Connector 6 -> L of the pump terminal
- Connector 7 -> N of the jet terminal
- Connector 8 -> L of the jet terminal
- Connector 9 -> N of the heater 1 terminal
- Connector 10 -> L of the heater 1 terminal
- Connector 11 -> N of the heater 2 terminal
- Connector 12 -> L of the heater 2 terminal

Then you have to connect together some wires from the upper terminal and isolate all non connected wires before powering up the spa.
- Cable A must be connected with cable C
- Cable B must be connected with cable D

You should get something like that :
![Board installed](/images/board_installed.jpg "Board installed")
