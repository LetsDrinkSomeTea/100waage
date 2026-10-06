FROM ubuntu:24.04

ENV DEBIAN_FRONTEND=noninteractive

RUN apt-get update && apt-get install -y --no-install-recommends \
    ca-certificates curl git python3 python3-serial \
    && rm -rf /var/lib/apt/lists/*

# arduino-cli fest gepinnt: Release-Archiv mit Pruefsumme statt curl | sh von master
ARG ARDUINO_CLI_VERSION=1.3.1
ARG ARDUINO_CLI_SHA256=376428d7d45be640c00812a71612e1742edc2f5f9ee3742a2d6da7870e079588
RUN curl -fsSL -o /tmp/arduino-cli.tgz \
      "https://github.com/arduino/arduino-cli/releases/download/v${ARDUINO_CLI_VERSION}/arduino-cli_${ARDUINO_CLI_VERSION}_Linux_64bit.tar.gz" \
 && echo "${ARDUINO_CLI_SHA256}  /tmp/arduino-cli.tgz" | sha256sum -c - \
 && tar -xzf /tmp/arduino-cli.tgz -C /usr/local/bin arduino-cli \
 && rm /tmp/arduino-cli.tgz

# Profil vorwaermen: Core und Bibliotheken in den festen Versionen aus
# waage/sketch.yaml landen im Image, spaetere Builds laufen offline.
COPY waage/sketch.yaml /warm/waage/sketch.yaml
RUN printf 'void setup() {}\nvoid loop() {}\n' > /warm/waage/waage.ino \
 && arduino-cli compile --profile c3 /warm/waage \
 && rm -rf /warm

WORKDIR /repo
