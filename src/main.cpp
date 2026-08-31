#include "gt7_protocol.h"
#include "salsa20.h"

#include <arpa/inet.h>
#include <curl/curl.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <csignal>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <cerrno>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr uint16_t kReceivePort = 33740;
constexpr uint16_t kSendPort = 33739;
constexpr std::size_t kBatchSize = 20;
constexpr char kKeyText[] = "Simulator Interface Packet GT7 ver 0.0";
volatile std::sig_atomic_t running = 1;

std::string environment(const char* name, const char* fallback = "") {
    const char* value = std::getenv(name);
    return value == nullptr ? fallback : value;
}

class Line {
public:
    explicit Line(std::string measurement) : text_(std::move(measurement)) {}

    void integer(const char* name, int64_t value) {
        fieldPrefix(name);
        text_ += std::to_string(value);
        text_ += "i";
    }

    void floating(const char* name, double value) {
        fieldPrefix(name);
        std::ostringstream valueText;
        valueText << std::setprecision(9) << value;
        text_ += valueText.str();
    }

    void boolean(const char* name, bool value) {
        fieldPrefix(name);
        text_ += value ? "true" : "false";
    }

    void string(const char* name, const std::string& value) {
        fieldPrefix(name);
        text_ += '"';
        for (char character : value) {
            if (character == '"' || character == '\\') {
                text_ += '\\';
            }
            text_ += character;
        }
        text_ += '"';
    }

    std::string finish() const { return text_; }

private:
    void fieldPrefix(const char* name) {
        text_ += fieldsStarted_ ? "," : " ";
        text_ += name;
        text_ += "=";
        fieldsStarted_ = true;
    }

    std::string text_;
    bool fieldsStarted_ = false;
};

class InfluxWriter {
public:
    InfluxWriter(std::string url, std::string org, std::string bucket, std::string token)
        : endpoint_(std::move(url) + "/api/v2/write?org=" + std::move(org) + "&bucket=" + std::move(bucket) + "&precision=ns"),
          token_(std::move(token)) {
        curl_global_init(CURL_GLOBAL_DEFAULT);
    }

    ~InfluxWriter() { curl_global_cleanup(); }

    bool write(const std::string& payload) const {
        CURL* curl = curl_easy_init();
        if (curl == nullptr) {
            return false;
        }
        struct curl_slist* headers = nullptr;
        headers = curl_slist_append(headers, "Content-Type: text/plain; charset=utf-8");
        headers = curl_slist_append(headers, ("Authorization: Token " + token_).c_str());
        curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload.c_str());
        curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, payload.size());
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 5L);
        const CURLcode result = curl_easy_perform(curl);
        long responseCode = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &responseCode);
        curl_slist_free_all(headers);
        curl_easy_cleanup(curl);
        if (result != CURLE_OK || responseCode < 200 || responseCode >= 300) {
            std::cerr << "InfluxDB write failed: " << curl_easy_strerror(result)
                      << " (HTTP " << responseCode << ")\n";
            return false;
        }
        return true;
    }

private:
    std::string endpoint_;
    std::string token_;
};

void stop(int) { running = 0; }

struct CollectorStats {
    uint64_t datagrams = 0;
    uint64_t validPackets = 0;
    uint64_t invalidPackets = 0;
    uint64_t duplicatePackets = 0;
    uint64_t sequenceGaps = 0;
    uint64_t influxWrites = 0;
    uint64_t influxFailures = 0;
    std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
    std::chrono::steady_clock::time_point lastReport = started;
};

int openSocket() {
    const int socketFd = socket(AF_INET, SOCK_DGRAM, 0);
    if (socketFd < 0) {
        throw std::runtime_error("Could not create UDP socket");
    }
    int reuse = 1;
    setsockopt(socketFd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    sockaddr_in localAddress{};
    localAddress.sin_family = AF_INET;
    localAddress.sin_addr.s_addr = htonl(INADDR_ANY);
    localAddress.sin_port = htons(kReceivePort);
    if (bind(socketFd, reinterpret_cast<sockaddr*>(&localAddress), sizeof(localAddress)) < 0) {
        close(socketFd);
        throw std::runtime_error("Could not bind UDP port 33740");
    }
    timeval timeout{1, 0};
    setsockopt(socketFd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    return socketFd;
}

void sendHeartbeat(int socketFd, const std::string& playstationIp, char packetType) {
    sockaddr_in destination{};
    destination.sin_family = AF_INET;
    destination.sin_port = htons(kSendPort);
    if (inet_pton(AF_INET, playstationIp.c_str(), &destination.sin_addr) != 1) {
        throw std::runtime_error("DT_PS5_IP is not a valid IPv4 address");
    }
    sendto(socketFd, &packetType, 1, 0, reinterpret_cast<sockaddr*>(&destination), sizeof(destination));
}

bool decodePacket(const uint8_t* encrypted, std::size_t byteCount, char packetType, PacketA& packet, PacketC& packetC) {
    const std::size_t expectedBytes = packetType == 'C' ? kPacketCBytes : kPacketABytes;
    if (byteCount != expectedBytes || byteCount < 0x44) {
        return false;
    }
    std::array<uint8_t, 32> key{};
    std::memcpy(key.data(), kKeyText, key.size());
    const uint32_t iv1 = static_cast<uint32_t>(encrypted[0x40]) |
                         (static_cast<uint32_t>(encrypted[0x41]) << 8) |
                         (static_cast<uint32_t>(encrypted[0x42]) << 16) |
                         (static_cast<uint32_t>(encrypted[0x43]) << 24);
    const uint32_t iv2 = packetType == 'C' ? iv1 ^ 0xDEADBEEFu : iv1 ^ 0xDEADBEAFu;
    std::array<uint8_t, 8> iv{
        static_cast<uint8_t>(iv2), static_cast<uint8_t>(iv2 >> 8), static_cast<uint8_t>(iv2 >> 16), static_cast<uint8_t>(iv2 >> 24),
        static_cast<uint8_t>(iv1), static_cast<uint8_t>(iv1 >> 8), static_cast<uint8_t>(iv1 >> 16), static_cast<uint8_t>(iv1 >> 24)
    };
    std::array<uint8_t, kPacketCBytes> decrypted{};
    gt7::Salsa20 cipher(key.data());
    cipher.setIv(iv.data());
    cipher.processBytes(encrypted, decrypted.data(), decrypted.size());
    std::memcpy(&packet, decrypted.data(), sizeof(packet));
    if (packetType == 'C') {
        std::memcpy(&packetC, decrypted.data(), sizeof(packetC));
    }
    return packet.magic == kGt7Magic;
}

std::string trackTime(int32_t milliseconds) {
    const int32_t seconds = milliseconds / 1000;
    return std::to_string(seconds / 60) + ":" + (seconds % 60 < 10 ? "0" : "") + std::to_string(seconds % 60);
}

std::string makeMeasurement(const PacketA& packet, const PacketC* packetC, double currentLap) {
    Line line("gt7");
    const double carSpeed = packet.speed * 3.6;
    const double tyreSpeeds[4] = {
        std::abs(3.6 * packet.tyreRadius[0] * packet.wheelRps[0]),
        std::abs(3.6 * packet.tyreRadius[1] * packet.wheelRps[1]),
        std::abs(3.6 * packet.tyreRadius[2] * packet.wheelRps[2]),
        std::abs(3.6 * packet.tyreRadius[3] * packet.wheelRps[3])
    };
    const double tyreDiameters[4] = {200 * packet.tyreRadius[0], 200 * packet.tyreRadius[1], 200 * packet.tyreRadius[2], 200 * packet.tyreRadius[3]};
    const double slip[4] = {carSpeed > 0 ? tyreSpeeds[0] / carSpeed : 0, carSpeed > 0 ? tyreSpeeds[1] / carSpeed : 0, carSpeed > 0 ? tyreSpeeds[2] / carSpeed : 0, carSpeed > 0 ? tyreSpeeds[3] / carSpeed : 0};
    const uint8_t currentGear = packet.gears & 0x0F;
    const uint8_t suggestedGear = packet.gears >> 4;

    line.integer("pktid", packet.packetId); line.integer("car_id", packet.carCode); line.string("track_time", trackTime(packet.dayProgression));
    line.integer("total_laps", packet.totalLaps); line.integer("current_position", packet.raceStartPosition); line.integer("total_positions", packet.preRaceNumCars);
    line.floating("best_lap", packet.bestLaptime / 1000.0); line.floating("last_lap", packet.lastLaptime / 1000.0); line.integer("current_lap_raw", packet.lapCount); line.floating("current_lap", currentLap);
    line.floating("car_speed", carSpeed); line.integer("estimate_top_speed", packet.calcMaxSpeed); line.floating("clutch", packet.clutch); line.floating("clutch_engaged", packet.clutchEngagement); line.floating("rpm_after_clutch", packet.rpmFromClutchToGearbox);
    line.floating("oil_temp", packet.oilTemp); line.floating("water_temp", packet.waterTemp); line.floating("oil_pressure", packet.oilPressure); line.floating("rpm", packet.engineRpm);
    line.integer("rpm_rev_warning", packet.minAlertRpm); line.integer("rpm_rev_limiter", packet.maxAlertRpm); line.floating("throttle", packet.throttle / 2.55); line.floating("breaking_force", packet.brake / 2.55);
    line.integer("cgear", currentGear); line.integer("sgear", suggestedGear);
    for (int index = 0; index < 8; ++index) line.floating((std::string("gear_") + std::to_string(index + 1)).c_str(), packet.gearRatios[index]);
    line.floating("above_gear_8", packet.transmissionTopSpeed); line.boolean("is_ev", packet.fuelCapacity <= 0); line.floating("fuel_capacity", packet.fuelCapacity); line.floating("fuel_remaining", packet.fuelLevel);
    line.floating("boost", packet.boost - 1); line.boolean("has_turbo", packet.boost > 0); line.floating("ride_height", packet.bodyHeight * 1000);
    for (int index = 0; index < 4; ++index) { const std::string wheel = std::string("FLFRRLRR").substr(index * 2, 2); line.floating(("suspension_" + wheel).c_str(), packet.suspHeight[index]); }
    for (int index = 0; index < 4; ++index) { const std::string wheel = std::string("FLFRRLRR").substr(index * 2, 2); line.floating(("tire_temp_" + wheel).c_str(), packet.tyreTemp[index]); line.floating(("tire_diameter_" + wheel).c_str(), tyreDiameters[index]); line.floating(("tire_diam_" + wheel).c_str(), packet.tyreRadius[index]); line.floating(("tire_speed_" + wheel).c_str(), tyreSpeeds[index]); line.floating(("tire_slip_ratio_" + wheel).c_str(), slip[index]); }
    line.floating("pos_X", packet.position[0]); line.floating("pos_Y", packet.position[1]); line.floating("pos_Z", packet.position[2]);
    line.floating("velocity_X", packet.worldVelocity[0]); line.floating("velocity_Y", packet.worldVelocity[1]); line.floating("velocity_Z", packet.worldVelocity[2]);
    line.floating("rot_pitch", packet.rotation[0]); line.floating("rot_yaw", packet.rotation[1]); line.floating("rot_roll", packet.rotation[2]); line.floating("angular_velocity_X", packet.angularVelocity[0]); line.floating("angular_velocity_Y", packet.angularVelocity[1]); line.floating("angular_velocity_Z", packet.angularVelocity[2]); line.floating("rotation", packet.orientationRelativeToNorth);
    if (packetC != nullptr) {
        line.string("packet_type", "C");
        line.string("surface_type", std::string(packetC->surfaceType, strnlen(packetC->surfaceType, sizeof(packetC->surfaceType))));
        line.integer("current_lap_ms", packetC->currentLap);
        line.floating("wheel_steering_angle_L", packetC->wheelSteeringAngle[0]);
        line.floating("wheel_steering_angle_R", packetC->wheelSteeringAngle[1]);
        line.floating("wheel_base", packetC->wheelBase);
        line.string("car_category", std::string(packetC->carCategory, strnlen(packetC->carCategory, sizeof(packetC->carCategory))));
        line.floating("wheel_rotation", packetC->wheelRotation);
        line.floating("steering_angular_velocity", packetC->steeringAngularVelocity);
        line.floating("sway", packetC->sway);
        line.floating("heave", packetC->heave);
        line.floating("surge", packetC->surge);
        line.floating("energy_recovery", packetC->energyRecovery);
    } else {
        line.string("packet_type", "A");
    }
    return line.finish();
}

} // namespace

int main() {
    std::signal(SIGINT, stop);
    std::signal(SIGTERM, stop);
    try {
        const std::string playstationIp = environment("DT_PS5_IP");
        const std::string influxUrl = environment("INFLUXDB_V2_URL", "http://localhost:8086");
        const std::string influxOrg = environment("INFLUXDB_V2_ORG", "initorg");
        const std::string influxBucket = environment("INFLUXDB_V2_BUCKET", "initbucket");
        const std::string influxToken = environment("INFLUXDB_V2_TOKEN");
        std::string packetType = environment("DT_PACKET_TYPE", "C");
        for (char& character : packetType) character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
        if (packetType != "A" && packetType != "C") throw std::runtime_error("DT_PACKET_TYPE must be A or C");
        const char requestedPacketType = packetType[0];
        if (playstationIp.empty() || influxToken.empty()) throw std::runtime_error("DT_PS5_IP and INFLUXDB_V2_TOKEN are required");
        InfluxWriter writer(influxUrl, influxOrg, influxBucket, influxToken);
        const int socketFd = openSocket();
        sendHeartbeat(socketFd, playstationIp, requestedPacketType);
        std::vector<std::string> batch;
        batch.reserve(kBatchSize);
        int32_t latestPacketId = -1;
        int16_t previousLap = -1;
        std::chrono::steady_clock::time_point lapStart = std::chrono::steady_clock::now();
        CollectorStats stats;
        std::array<uint8_t, 4096> receiveBuffer{};
        std::cerr << "Listening for GT7 packet " << requestedPacketType << " at 60 Hz\n";
        const auto reportStats = [&]() {
            const auto now = std::chrono::steady_clock::now();
            if (now - stats.lastReport < std::chrono::seconds(10)) return;
            const double elapsed = std::chrono::duration<double>(now - stats.started).count();
            const double rate = stats.validPackets / elapsed;
            std::cerr << "[stats] type=" << requestedPacketType << " rate=" << std::fixed << std::setprecision(1) << rate
                      << " Hz expected=60.0 valid=" << stats.validPackets << " datagrams=" << stats.datagrams
                      << " invalid=" << stats.invalidPackets << " duplicates=" << stats.duplicatePackets
                      << " sequence_gaps=" << stats.sequenceGaps << " influx_writes=" << stats.influxWrites
                      << " influx_failures=" << stats.influxFailures << "\n";
            stats.lastReport = now;
        };
        while (running) {
            const ssize_t received = recvfrom(socketFd, receiveBuffer.data(), receiveBuffer.size(), 0, nullptr, nullptr);
            ++stats.datagrams;
            reportStats();
            if (received < 0) {
                if (errno == EAGAIN || errno == EWOULDBLOCK) { sendHeartbeat(socketFd, playstationIp, requestedPacketType); }
                if (errno == EINTR) continue;
                if (errno != EAGAIN && errno != EWOULDBLOCK) throw std::runtime_error("UDP receive failed");
                continue;
            }
            PacketA packet{};
            PacketC packetC{};
            if (!decodePacket(receiveBuffer.data(), static_cast<std::size_t>(received), requestedPacketType, packet, packetC)) { ++stats.invalidPackets; continue; }
            if (packet.packetId <= latestPacketId) { ++stats.duplicatePackets; continue; }
            if (latestPacketId >= 0 && packet.packetId > latestPacketId + 1) stats.sequenceGaps += packet.packetId - latestPacketId - 1;
            ++stats.validPackets;
            latestPacketId = packet.packetId;
            if (packet.lapCount != previousLap) { previousLap = packet.lapCount; lapStart = std::chrono::steady_clock::now(); }
            const double currentLap = std::chrono::duration<double>(std::chrono::steady_clock::now() - lapStart).count();
            batch.push_back(makeMeasurement(packet, requestedPacketType == 'C' ? &packetC : nullptr, currentLap));
            if (batch.size() >= kBatchSize) {
                std::string payload;
                for (const auto& point : batch) payload += point + "\n";
                if (writer.write(payload)) ++stats.influxWrites; else ++stats.influxFailures;
                batch.clear();
            }
            if (packet.packetId % 100 == 0) sendHeartbeat(socketFd, playstationIp, requestedPacketType);
        }
        if (!batch.empty()) { std::string payload; for (const auto& point : batch) payload += point + "\n"; writer.write(payload); }
        close(socketFd);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
