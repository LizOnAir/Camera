# Camera

This camera consist of a custom PCB and buttons as well as a 2.4 inch tft display with sd card slot, that can capture picture. I made this project to improve my knowledge about PCB design and also I personally really wanna make a retro camera with noisy resolution. Through this project, I learnt about high speed signals, parallel signals and heat management.

<img width="274" height="389" alt="Screenshot 2026-04-14 at 4 07 00 PM" src="https://github.com/user-attachments/assets/f05de6a7-5195-43b5-a490-7520836e129f" />

## Features:
- auto-focus 
- power with battery
- capture photos
- view photos

## CAD Model:
<img width="277" height="268" alt="Screenshot 2026-04-10 at 10 55 10 AM" src="https://github.com/user-attachments/assets/dccc066b-072e-4523-b5c6-fec719a07dd6" />
<img width="355" height="268" alt="Screenshot 2026-04-10 at 10 56 41 AM" src="https://github.com/user-attachments/assets/30788fd6-1dcf-4579-967d-c2d470965710" />
<img width="355" height="203" alt="Screenshot 2026-04-10 at 10 56 52 AM" src="https://github.com/user-attachments/assets/1e1d4333-79a0-40e6-9789-0100882b69d5" />

Made in Fusion360.Liz
Access files in [CAD folder](https://github.com/LizOnAir/Camera/tree/main/CAD).

## Assembly
Steps:
1.  Insert 4 heatset onto the 4 mounting holes in the bottom case.
2.  Insert the PCB into the bottom case.
3.  Using hot glue, stick the buttons onto the tactile switches.
4.  Align the holes of PCB onto the holes of the bottom case.
5.  Cover the top case over the bottom case.
6.  Screw them tight together.
7.  Charge the battery and use!!

## PCB
Here's my PCB! It was made in KiCad. 
### Schematic
Main board

<img width="1382" height="922" alt="image" src="https://github.com/user-attachments/assets/066cfecf-ed3b-4aff-92a4-a1a86fc6c304" />

Camera - OV5640

<img width="1088" height="690" alt="image" src="https://github.com/user-attachments/assets/e5012f62-585e-48c8-985d-8da755ac2726" />

### PCB
<img width="1088" height="1002" alt="image" src="https://github.com/user-attachments/assets/cdf5980a-804a-4166-94f4-8f27efdb36bb" />

Access files in [PCB folder](https://github.com/LizOnAir/Camera/tree/main/PCB).

## Firmware
I use Arduino IDE to program this camera
- dual state
    - capture image
    - review image

Access files in [Firmware folder](https://github.com/LizOnAir/Camera/tree/main/Firmware).

## BOM:
Here should be [everything](https://docs.google.com/spreadsheets/d/14nMMweMc_XzQDbo61nnmqZUTtTscH9UbB8BGQstIzH4/edit?usp=sharing) you need to make this:
- 1x PCBA
- 1x Ov5640
- 1x 3.7V Lipo Battery 500mAh
- 3x Tactile buttons
- 1x Slide switch
- 1x PH connector
- 1x Micro SD card
- 2.4 inch tft display
- 4x M3 14mm Screws
- 4x M3 OD4.2 L 4 Heatset inserts
- 1x 3d printed case


