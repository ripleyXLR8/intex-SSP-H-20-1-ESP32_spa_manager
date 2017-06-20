# DRAFT - WORK IN PROGRESS

# Intex SSP-H-20-1 Arduino Spa Manager

This project aim to provide a replacement for the motherboard of the Intex SSP-20 motherboard. My wish was to be able to remote control the spa and integrate it in my domotic system (Jeedom but it should work with other system). The second goal of this project is to improve the reliability of this spa and specificaly to solve a random E95 error problem. My spa was still under waranty when i started this project so I tried to be as stealth as possible...

## 1) Intex motherboard reverse engineering
Below the annotate picture of the motherboard. The board is allready annotate there is some little "tricks"

## 2) Designing the replacement board
### a) Electrical drawing
### b) Board schematic
I wanted the board to be easily produce at home by anybody so I choose to use a single side PCB with plated-trhough holes (PTH) components. SMD component will be a also good choice, but I know that occasionnal welder are more at ease with PTH components. The drilling size is 0.8mm, the width of the electrical tracks is 16mil (0.40mm) and the clearance is 10mil (0.25mm), theses parameters allows you to produce the board using the toner transfer method (this method requires very few tools). There is only 2 air wires on the current version of the board (you can propose a new version to reduce this number).

I choose to use a pre-build 4 relays board because I had some laying around... but you can integrate relays to the main PCB, it will look more professionnal.

## 3) Writing the code

## 4) Upload and first run

