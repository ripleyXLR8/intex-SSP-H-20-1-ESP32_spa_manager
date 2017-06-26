# WORK IN PROGRESS

# Intex SSP-H-20-1 Arduino Spa Manager

**WARNING : The circuit in this project uses main voltage (220 VAC in western Europe). This can be deadly if not handled properly! You can easily hurt yourself as well. Build this circuit at your own risk.**

This project aims to provide a replacement for the motherboard of the Intex SSP-20 motherboard. My wish was to be able to remote control the spa and integrate it in my domotic system (Jeedom but it should work with other system since it rely on MQTT protocol). The second goal of this project is to improve the reliability of this spa and specificaly to solve a random E95 error problem. My spa was still under waranty when i started this project so I tried to be as stealth as possible...

## 1) Intex motherboard reverse engineering
Below the annotate picture of the motherboard with my understanding of its working principle.

![Motherboard schematic](/images/motherboard_schematic.png)

### a) Sensors
There are 5 sensors connected to the board, 2 temperature sensors, 2 flow sensors and 1 temperature fuse. They are all connected with a kind of JST connector, it looks like a JST XH but the JST XH doesn't have the lock mecanism. For the moment I use a standart 2.54mm pithc male pin header to connect them.

#### Temperature sensors
Both temperature sensors are Negative Temperature Coefficient (NTC) thermistore, it means that the resistance of the thermistore decreases with an increase in temperature in a non linear way. I use the code available on this page (http://www.circuitbasics.com/arduino-thermistor-temperature-sensor-tutorial/) to read the temperature from the thermistore. This code is based on the Steinhart-Hart equation and to work at his best, the coeficient of this equation must be adapted to your thermistore. But I did not want to unscrew the thermistore embedded in my spa so I use the standart coeficient of the equation and it works fine with an acceptable accuracy.

The temperature sensor 1 is installed at the intake of the water pump so it reads the water temperature and it is use to regulate the water temperature from 20°C to 40°C.

The temperature sensor 2 is installed in the core of  heating elements and its function is to detect overheating over 50°C and shut down the heating if necessary.

#### Temperature fuse
One temperature fuse is installed just below heating elements. I cannot test the triggering temperature but it seems that in normal condition the fuse let the current pass and if the temperature goes over 84°C then the fuse burn and the current stop passing and shut down the heating.

#### Flow sensors
Both flow sensors let the current pass only if flow is detected, they ensure that no heating is activated if there is no flow detected in the system.

### b) Power supply, relays and other stuffs
All the power cable (Input and outputs) are connected by a barrier terminal block. We will use the same connector on our board because we don't want to alter the power cable and change their connector.

This is one of the most uncommon part of this project, you will find below in the Bill Of Quantities section some link to find it.

#### Heating elements
There is two heating element in the spa for a total power of 2000Watts. They are directly powered by a relay allowing 220VAC to pass trough the heating element.
Please note that in normal condition :
- The water pump is allways powered when heating. Any flow interruption by the flow sensor result in an error and the heating stops.
- When you push the heating button the heating doesn't start immediately, a primary relay is first activated (I don't know what is the true use of tis primary relay), the water pump starts and then a couple seconds after the heater 1 is powered then a couple seconds later the heater 2 starts.
- The heater 2 is shut-down if the jet pump is ON.

#### Jet pump
The jet pump is directly powered by a relay allowing 220VAC to pass trough the pump and pumping air into the Spa. (Nothing fancy)

#### Water pump
The water pump is powered by 12VAC but before that a relay allows 220VAC to flow through a dual output 12VAC transformer (see the drawing above). One of the output is connected to a relay but activate this relay, the second output of the transformer goes into a full bridge rectifier producing 12VDC and to the relay. The 12VAC is then allowed to flow into the water pump.

#### Control panel
I did not investigate the control panel operation.

#### Descaler system
It seems that this spa is equiped by a magnetic descaling system. Here is the wikipedia page on this technology (https://en.wikipedia.org/wiki/Magnetic_water_treatment). I cannot find the type of signal send to this device, it seems that the signal is AC with a modulation of amplitude. I will investigate it later, for the moment, this function has not been implemented on the replacement board.

## 2) Designing the replacement board
### a) Working principle
An Arduino Nano is the heart of the system. It allows us to connect : 
- 2 temperature sensors on 2 analog inputs
- 2 flow sensors on 2 digital inputs
- 1 temperature fuse on 1 digital input
- 5 buttons on 5 digital input (PUMP, HEATING, JET, +, -)
- 1 LCD display on 2 digital inputs (I2C protocol)
- 4 relays on a board on 4 digital inputs
- 1 ESP8266-01 on 2 digital inputs

The relay board, the LCD and the ESP8266 will not be powered from the 5Volt pin of the arduino, we will use a 5VDC power supply to power them directly.

### b) Electrical drawing
I used Eagle 8.2.2 to design the replacement board, below is the electrical drawing. The eagle files will be available soon.

![Electrical schematic](/images/electrical_schematic.png)

### c) Board schematic
I wanted the board to be easily produce at home by anybody so I choose to use a single side PCB with plated-trhough holes (PTH) components. SMD component will be a also good choice, but I know that occasionnal welder are more at ease with PTH components. The drilling size is 0.8mm, the width of the electrical tracks is 16mil (0.40mm) and the clearance is 10mil (0.25mm), theses parameters allows you to produce the board using the toner transfer method (this method requires very few tools). There is only 2 air wires on the current version of the board (you can propose a new version to reduce this number).

~~I choose to use a pre-build 4 relays board because I had some laying around... but you can integrate relays to the main PCB, it will look more professionnal.~~ Relays are integrated on the board to integrate more easily in the original enclosure.

The original motherboard dimension are 10.5cm x 17cm, mounting hole are 5mm wide and are placed 4.5mm from the edge of the board. The PCB will be mounted using the same mounting screw used by the original motherboard (but I don't think I will be using the center plastic lock).

### d) Board production

https://www.reichelt.de/my/1360986

## 3) Writing the code

## 4) Upload and first run

