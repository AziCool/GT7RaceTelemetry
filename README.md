# GT7RaceTelemetry

Project for high-frequency GT7 telemetry.

A native C++ UDP collector for Gran Turismo 7 telemetry on PlayStation 5. It is based on the packet layout and Salsa20 protocol from [MacManley/gt7-udp](https://github.com/MacManley/gt7-udp), and writes the existing `gt7` measurement contract used by [data-tools-transmit](https://github.com/shawnazar/data-tools-transmit) into InfluxDB.

## Quick start

1. Copy `.env.example` to `.env`.
2. Set `DT_PS5_IP` to the PS5's static IPv4 address.
3. Replace the example InfluxDB and Grafana passwords/tokens.
4. Start the stack:

```sh
docker compose up --build -d
```

Grafana is available at `http://localhost:3000` and InfluxDB at `http://localhost:8086`. The collector listens on UDP port `33740` and sends the heartbeat to the PS5 on UDP port `33739`.

## Architecture

The collector listens for encrypted GT7 packet A datagrams, decrypts them with Salsa20, validates the GT7 magic value, maps the packet into the legacy dashboard field names, and batches 20 line-protocol records per InfluxDB HTTP request. It sends a heartbeat on startup, every 100 packets, and after a receive timeout.

The Grafana dashboard and Flux datasource are provisioned from `config/grafana`. Packet A is selected because it is available in all GT7 driving modes, including Sport Mode. Packet B, `~`, and C can be added later by extending the packet layouts and selecting the corresponding heartbeat variant.

## Local build

The collector targets Linux because the deployment uses Docker. With CMake, a C++17 compiler, and libcurl development headers installed:

```sh
cmake -S . -B build
cmake --build build --config Release
```

## Compatibility note

The upstream Arduino library uses `WiFiUDP` and `IPAddress`, so it cannot be linked directly into a Linux container. This project ports its protocol and packet layout to a POSIX UDP socket while preserving its Salsa20 implementation behavior and the existing InfluxDB/Grafana field contract.
