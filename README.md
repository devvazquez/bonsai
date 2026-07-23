# Bonsai
The main repository containing all the files involved in the creation of these smart glasses, such as cad files, code, designs...

<img height="720" alt="IMG_3797" src="https://github.com/user-attachments/assets/816af56a-fea3-4214-b69e-b98444782122" />
<img width="1280" height="640" alt="22877814-e29b-4794-9276-75eacdae1a95" src="https://github.com/user-attachments/assets/1a2a9ede-5398-45c5-bfc8-b07443b2d9db" />
<img width="669" height="351" alt="{5F14CA35-08FD-4233-AC37-D65E22D8F3A3}" src="https://github.com/user-attachments/assets/e279554b-d24b-49e6-b1a2-0b9cd7bb613b" />


## Repository Structure

```
Bonsai-Glasses/
├── bom.csv             # The bill of materials with links.
├── src/
│   ├── firmware/       # Embedded / microcontroller source code
│   └── software/       # Host-side application and companion software
├── hardware/
│   ├── cad/            # 3D CAD models and mechanical designs
│   └── bonsai_pcb/     # PCB schematics and board layouts
└── assets/             # Images, logos, and other design assets
```

## Bill of Materials
| Name | Purpose | Quantity | Total Cost (USD) | Link | Distributor |
| --- | --- | --- | --- | --- | --- |
| 3.7V Lipo Battery | Powering the device. | | $10.50 | [Buy](https://www.aliexpress.us/item/3256808990377891.html) | Aliexpress |
| JST PH 2.0 Connector | Connect battery to PCB (Bent pin socket, 2P) | | $1.95 | [Buy](https://www.aliexpress.us/item/3256807255498839.html) | Aliexpress |
| Seeed Studio XIAO ESP32S3 Sense | Tiny devboard with camera support (ESP32S3 Sense Module) | | $20.00 | [Buy](https://es.aliexpress.com/item/1005005544221475.html) | Aliexpress |
| MAX98357A I2S Audio Amplifier | Drives the speakers since ESP32 lacks enough power directly | | $1.68 | [Buy](https://www.aliexpress.us/item/3256812158409627.html) | Aliexpress |
| 75MM OV3660 120° Camera | Captures image for ESP32 to process (120 Degrees GOOD) | | $6.89 | [Buy](https://es.aliexpress.com/item/1005009339247009.html) | Aliexpress |

![Cart Image](https://raw.githubusercontent.com/devvazquez/bonsai/refs/heads/main/assets/cart-image.png)
