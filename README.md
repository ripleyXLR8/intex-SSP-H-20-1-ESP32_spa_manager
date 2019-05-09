# WORK IN PROGRESS

# Intex SSP-H-20-1 ESP32 Spa Manager

**WARNING 1 : The circuit in this project uses main voltage (220-240 Volt AC in western Europe). This can be deadly if not handled properly! You can easily hurt yourself as well. Build this circuit at your own risk.**

**WARNING 2 : This project is still a work in progress, this page is a kind of building log, I haven't finish the board building and installation for the moment. Watch the issues page to see what is working and what is not.**

This project aims to provide a replacement for the motherboard of the Intex SSP-20 motherboard. My wish was to be able to control the spa remotely and integrate it in my domotic system. I'm using Jeedom https://www.jeedom.com/, an open-source system, but it should work with other system since it rely on MQTT protocol and a rest server (based on the Arest library - https://github.com/marcoschwartz/aREST). The second goal of this project is to improve the reliability of this spa and specificaly to solve a random E95 or E9X error problem. My spa was still under waranty when i started this project so I tried to be as stealth as possible.

## 1) Intex motherboard reverse engineering
Below the annotate picture of the motherboard with my understanding of its working principle. Since the card is labeled, everything is pretty straight forward except the descaler system and the pump system (the upper terminal barrier connector on the board).

![Motherboard schematic](/images/motherboard_schematic.png)

### a) Sensors
There are 5 sensors connected to the board, 2 temperature sensors, 2 flow sensors and 1 temperature fuse. They are all connected with a type of 3 pins JST connector, it looks like a JST XH 3P but the JST XH 3P doesn't have the lock mecanism.

#### Temperature sensors
Both temperature sensors are negative temperature coefficient (NTC) thermistore, it means that the resistance of the thermistore decreases with an increase in temperature in a non linear way. I use the code available on this page (http://www.circuitbasics.com/arduino-thermistor-temperature-sensor-tutorial/) to read the temperature from the thermistore. This code is based on the Steinhart-Hart equation (https://fr.wikipedia.org/wiki/Relation_de_Steinhart-Hart) and to work at his best, the coeficient of this equation must be adapted to your thermistore. Since there is 3 coefficients in this equation, you will need 3 points to determine the coefficients.

The temperature sensor 1 is installed at the intake of the water pump so it reads the water temperature and it is use to regulate the water temperature from 20°C to 40°C.

The temperature sensor 2 is installed in the core of  heating elements and its function is to detect overheating over 50°C and shut down the heating if necessary.

To measure the real temperature of the water in the spa I connected a DS18B20 thermal sensor in place of the LCD panel. Then I have recorded the temperature of the water, and the resistance of both integrated temperature probe of the spa during the heating process of the spa from 18°C to 40°C. The resut once smoothed is the curve presented below. With this curve we can determine the 3 coefficients of the Steinhart-Hart equation.

[TO DO : INSERT CURVE + CALCUL COEF]

#### Temperature fuse
One temperature fuse is installed just below heating elements. I cannot test the triggering temperature but it seems that in normal condition the fuse let the current pass and if the temperature goes over 84°C then the fuse burn and the current stop passing and shut down the heating.

#### Flow sensors
Both flow sensors let the current pass only if flow is detected, they ensure that no heating is activated if there is no flow detected in the system.

### b) Power supply, relays and other stuffs
All the power cable (input and outputs) are connected by a barrier terminal block. We will use the same connector on our board because we don't want to alter the power cable and change their connector. Please note that this barrier connector is one of the most difficult part to find for this project. I cannot find it on https://www.reichelt.de/ which is my usual parts supplier, but I find on http://www.newark.com/.

#### Heating elements
There is two heating element in the spa for a total power of 2000Watts. They are directly powered by a relay allowing 220VAC to pass trough the heating element.
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
It seems that this spa is equiped by a magnetic descaling system. Here is the wikipedia page on this technology (https://en.wikipedia.org/wiki/Magnetic_water_treatment). I cannot find the type of signal send to this device, it seems that the signal is AC with a modulation of amplitude. I will investigate it later because for the moment I don't have an oscilloscope so this function has not been implemented on the replacement board.

## 2) Designing the replacement board
### a) Working principle
An ESP32 is the heart of the system. It allows us to connect : 
- 2 temperature sensors on 2 analog inputs,
- 2 flow sensors on 2 digital inputs,
- 1 temperature fuse on 1 digital input,
- 5 buttons on 5 digital input (PUMP, HEATING, JET, +, -),
- 1 LCD display on 2 digital inputs (I2C protocol),
- 4 power relays on 4 digital inputs.

Relays, the LCD and the ESP32 will not be powered from the 5 Volt pin of the arduino, we will use directly the 5 Volt DC power supply to power them directly.

It will also allow us to connect to the wifi and the MQTT server to send and receive data.

### b) Electrical drawing
I used Eagle 8.2.2 to design the replacement board, below is the electrical drawing. The eagle files will be available soon.

![Electrical schematic](/images/electrical_schematic.png)

### c) Board schematic
I wanted the board to be easily produce at home by anybody so I choose to use a single side PCB with plated-trhough holes (PTH) components. SMD component will be a also good choice, but I know that occasionnal welder are more at ease with PTH components. The drilling size is 0.8mm, the width of the electrical tracks is 16mil (0.40mm) and the clearance is 10mil (0.25mm), theses parameters allows you to produce the board using the toner transfer method (this method requires very few tools). There is only 2 air wires on the current version of the board (you can propose a new version to reduce this number).

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


## 3) Writing the code

I'm using the Arduino IDE to write and upload code to the ESP32. The lastest release of the code can be found in this directtory.

## 4) Upload and first run

I have connected the board to the spa on May 1st 2019. Main functions are working. See the "issues" page to see whhat is working and what is not. A new version of the board will be designed to solve some problems.

## 5) Configuration and installation of the new control board
### a) Configuring the new board and uploading the code
Open the lastest *.ino file within the arduino IDE and configure the following variables to your needs.

`const bool wifi_enable = true;`
`const char* ssid     = "YOUR_WIFI_NETWORK_NAME";`
`const char* password = "YOUR_WIFI_NETWORK_SECURITY_KEY"`

**Please note that in the current state of developpement you must activate WiFi in order to control the spa. The local commands are not working properly.**

if you want to use mqtt you must set :
`const bool mqtt_enable = true;`
`const char* mqttServer = "IP_OF_YOUR_MQTT_SERVER";`
`const int mqttPort = PORT_OF_YOUR_MQTT_SERVER;`

if you want to use WiFi OTA (a very usefull feature that will allow you to upload any update of the code over the air, wihout opening the case) you must set :
`const bool ota_enable = true;`

Place your ESP32 on a breadboard and upload the code. Once the upload is finish press the reset button of the board.

### a) Checking the board WiFi connection
Open the serial port monitor of the Arduino IDE and set the baudrate to 115200. You should see the start-up and the IP of your board on the WiFi network.

If you have any error message during the start-up you should check your board configuration.

Once the board is connected to the wifi network you should open the following URL : `http://IP-OF-YOUR-BOARD`.You should get a JSON chain giving you the current state of the board. It should looks like this :


### b) Removing the old control board
First, it is very important to DISCONNECT THE POWER CABLE of the Spa. Remove the four screws securing the spa cover and remove it. Then locate the cover of the control board, remove all the screws and remove the cover. On the control board remove all cable from the lower and upper terminal, from the sensors and from the control panel, then remove the four screws securing the board and remove it.

### c) Installation of the new control board
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
