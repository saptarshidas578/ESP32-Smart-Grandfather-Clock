# ESP32 Smart Grandfather Clock

A custom Smart Grandfather Clock built on the ESP32 that combines real-time audio synthesis, dual-core task scheduling, LED visualization, RTC-based timekeeping, and automatic NTP synchronization.

## Features

* 🔔 Authentic Westminster chime sequence
* 🎵 Real-time bell synthesis using harmonic modeling and decay envelopes
* ⚡ Dual-core architecture using FreeRTOS
* 🌈 84-LED WS2812B clock display with dynamic color themes
* 🌙 Automatic Night Mode for silent operation during sleeping hours
* 🕒 DS1307 RTC backup timekeeping
* 🌐 Automatic NTP synchronization for long-term accuracy
* 🔄 Non-blocking background Wi-Fi synchronization
* 🎚️ PCM5100A I2S DAC audio output

## Hardware Used

* ESP32 Development Board
* PCM5100A I2S DAC
* DS1307 RTC Module
* WS2812B LED Matrix (84 LEDs)
* Amplifier and Speaker
* 5V Power Supply

## Architecture

The project leverages the ESP32's dual-core processor:

### Core 0

* Audio synthesis engine
* Westminster chime playback
* Background Wi-Fi and NTP synchronization

### Core 1

* Clock logic
* LED rendering
* RTC management
* User-facing functionality

This separation ensures smooth animations and uninterrupted audio playback even during network operations.

## Technical Highlights

* FreeRTOS task pinning
* Custom additive audio synthesis
* Harmonic generation and exponential decay envelopes
* Non-blocking state-machine-based network synchronization
* Automatic RTC correction from NTP time sources

## Future Improvements

* Configurable chime schedules
* Web-based settings interface
* OTA firmware updates
* Additional chime patterns and sound profiles

## License

MIT License
