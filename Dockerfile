FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive
ENV PATH="/root/bin:${PATH}"

RUN apt-get update && apt-get install -y \
    curl python3 python3-serial \
    && rm -rf /var/lib/apt/lists/*

# Install arduino-cli
RUN curl -fsSL https://raw.githubusercontent.com/arduino/arduino-cli/master/install.sh | sh

# Write config directly — avoids arduino-cli config syntax differences
RUN mkdir -p /root/.arduino15 && printf 'board_manager:\n  additional_urls:\n  - https://espressif.github.io/arduino-esp32/package_esp32_index.json\n' \
    > /root/.arduino15/arduino-cli.yaml

# Install ESP32 core (slow layer — cached unless Dockerfile changes)
RUN arduino-cli core update-index && \
    arduino-cli core install esp32:esp32

# Install required libraries
RUN arduino-cli lib install \
    "Adafruit SSD1306" \
    "Adafruit GFX Library" \
    "Adafruit BusIO" \
    "HX711"

WORKDIR /sketch
