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

Grafana is available at `http://localhost:3000` and InfluxDB at `http://localhost:8086`. The collector listens on UDP port `33740` and sends the selected packet heartbeat to the PS5 on UDP port `33739`.

Set `DT_PACKET_TYPE=C` to request the 368-byte C packet variant. Use `DT_PACKET_TYPE=A` for the legacy 296-byte packet if C is unavailable in the current GT7 mode.

To monitor reception, run `docker compose logs -f collector`. Every 10 seconds the collector reports its measured rate, expected 60 Hz rate, invalid packets, duplicate packets, sequence gaps, and InfluxDB write results. `rate` should be close to 60 Hz while GT7 is actively sending telemetry, and `invalid`, `sequence_gaps`, and `influx_failures` should remain zero.

## Architecture

The collector listens for encrypted GT7 packet A or C datagrams, decrypts them with Salsa20, validates the GT7 magic value, maps the packet into the legacy dashboard field names, and batches 20 line-protocol records per InfluxDB HTTP request. C also stores surface type, current lap time, wheel steering angle, wheel base, car category, wheel dynamics, and energy recovery fields.

The Grafana dashboard and Flux datasource are provisioned from `config/grafana`. Packet C is selected by default because it includes the previous packet data plus additional race and car metrics.

## Local build

The collector targets Linux because the deployment uses Docker. With CMake, a C++17 compiler, and libcurl development headers installed:

```sh
cmake -S . -B build
cmake --build build --config Release
```

## Compatibility note

The upstream Arduino library uses `WiFiUDP` and `IPAddress`, so it cannot be linked directly into a Linux container. This project ports its protocol and packet layout to a POSIX UDP socket while preserving its Salsa20 implementation behavior and the existing InfluxDB/Grafana field contract.
