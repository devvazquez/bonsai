<img width="3078" height="548" alt="image" src="https://github.com/user-attachments/assets/18338af4-06c8-4e27-90f2-faf2f699a424" />

The main repository containing all the files involved in the creation of this smart device, such as cad files, code, designs...

## Motivation for the project
I've always wanted to do a hardware project like this, involving a camera. And I love the idea of being able to connect AI with real-world data, so this project is perfect!

## Repository Structure

```
Bonsai-Glasses/
├── bom.csv             # The bill of materials with links.
├── src/
│   ├── firmware/       # Embedded / microcontroller source code
├── hardware/
│   ├── cad/            # 3D CAD models and mechanical designs
│   └── bonsai_pcb/     # PCB schematics and board layouts
└── assets/             # Images, logos, and other design assets
```

## How it works (WIP)
Thanks to the [backend](https://github.com/devvazquez/bonsai-backend), Bonsai is able to interact easily with **free and fast** AI providers, such as Groq (for the STT and vision LLM). For TTS, it uses Piper (locally), which is esential for **Catalan** support. 
Thanks to the board, a XIAO ESP32 Sense, we can use the provided camera to give the model visual context, which is then used to describe the current enviroment.
Using the built-in web server in the firmware, you can configre all the Wifi networks, and default user button actions. You can also change parameters such as default language. Some of these parameters can also be configured using the wake word (ex. "Hey Bonsai, change the language to Spanish"). 

## Bill of Materials
| Name | Purpose | Quantity | Total Cost (USD) | Link | Distributor |
| --- | --- | --- | --- | --- | --- |
| 3.7V Lipo Battery | Powering the device. | | $10.50 | [Buy](https://www.aliexpress.us/item/3256808990377891.html) | Aliexpress |
| JST PH 2.0 Connector | Connect battery to PCB (Bent pin socket, 2P) | | $1.95 | [Buy](https://www.aliexpress.us/item/3256807255498839.html) | Aliexpress |
| Seeed Studio XIAO ESP32S3 Sense | Tiny devboard with camera support (ESP32S3 Sense Module) | | $20.00 | [Buy](https://es.aliexpress.com/item/1005005544221475.html) | Aliexpress |
| MAX98357A I2S Audio Amplifier | Drives the speakers since ESP32 lacks enough power directly | | $1.68 | [Buy](https://www.aliexpress.us/item/3256812158409627.html) | Aliexpress |
| 75MM OV3660 120° Camera | Captures image for ESP32 to process (120 Degrees GOOD) | | $6.89 | [Buy](https://es.aliexpress.com/item/1005009339247009.html) | Aliexpress |
| Push Button | Taking the photo | | 2.95$ | [Buy](https://www.aliexpress.us/item/3256804762387991.html) | Aliexpress |
| Small Speaker | Playing the TTS sound | | 4.53$ | [Buy](https://www.aliexpress.us/item/3256811556916339.html) | Aliexpress |
| PCB | Holding all the components together | | 23$ | [Buy](https://jlcpcb.com/) | JLPCB |
