# Hardware Wiring & Interconnects

## Pin Allocation

| Module | Pin Name | ESP32 GPIO | Description |
|---|---|---|---|
| **I2S DAC (MAX98357A / PCM5102A)** | Word Select (WS / LRCK) | **GPIO 25** | Left/Right channel audio clock |
| **I2S DAC** | Bit Clock (BCK / BCLK) | **GPIO 26** | Audio bit clock |
| **I2S DAC** | Data (DOUT / DIN) | **GPIO 27** | 16-bit PCM audio stream |
| **FastLED WS2812B Ring** | Data (DIN) | **GPIO 5** | 800 kHz NRZ data for 84 addressable LEDs |
| **DS1307 RTC** | SDA | **GPIO 21** | I2C Serial Data |
| **DS1307 RTC** | SCL | **GPIO 22** | I2C Serial Clock |

## Power Distribution
- 5V DC supply (min 2.0A) connected to VBUS/5V and GND.
- 1000 µF electrolytic capacitor recommended across 5V and GND near LED strip power input to buffer current spikes.
- 330-470 Ω series resistor on GPIO 5 data line to protect first WS2812B LED.
