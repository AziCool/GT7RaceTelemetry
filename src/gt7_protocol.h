#pragma once

#include <cstdint>
#include <cstddef>

#pragma pack(push, 1)

enum class SimulatorFlags : int16_t {
    None = 0,
    CarOnTrack = 1 << 0,
    Paused = 1 << 1,
    LoadingOrProcessing = 1 << 2,
    InGear = 1 << 3,
    HasTurbo = 1 << 4,
    RevLimiterBlinkAlertActive = 1 << 5,
    HandBrakeActive = 1 << 6,
    LightsActive = 1 << 7,
    HighBeamActive = 1 << 8,
    LowBeamActive = 1 << 9,
    ASMActive = 1 << 10,
    TCSActive = 1 << 11
};

struct PacketA {
    int32_t magic;
    float position[3];
    float worldVelocity[3];
    float rotation[3];
    float orientationRelativeToNorth;
    float angularVelocity[3];
    float bodyHeight;
    float engineRpm;
    uint8_t iv[4];
    float fuelLevel;
    float fuelCapacity;
    float speed;
    float boost;
    float oilPressure;
    float waterTemp;
    float oilTemp;
    float tyreTemp[4];
    int32_t packetId;
    int16_t lapCount;
    int16_t totalLaps;
    int32_t bestLaptime;
    int32_t lastLaptime;
    int32_t dayProgression;
    int16_t raceStartPosition;
    int16_t preRaceNumCars;
    int16_t minAlertRpm;
    int16_t maxAlertRpm;
    int16_t calcMaxSpeed;
    SimulatorFlags flags;
    uint8_t gears;
    uint8_t throttle;
    uint8_t brake;
    uint8_t padding;
    float roadPlane[3];
    float roadPlaneDistance;
    float wheelRps[4];
    float tyreRadius[4];
    float suspHeight[4];
    float unknownFloats[8];
    float clutch;
    float clutchEngagement;
    float rpmFromClutchToGearbox;
    float transmissionTopSpeed;
    float gearRatios[8];
    int32_t carCode;
};

struct PacketB : public PacketA {
    float wheelRotation;
    float steeringAngularVelocity;
    float sway;
    float heave;
    float surge;
};

struct PacketTilda : public PacketB {
    uint8_t throttleFiltered;
    uint8_t brakeFiltered;
    uint8_t unknownUint81;
    uint8_t unknownUint82;
    float torqueVectors[4];
    float energyRecovery;
    float unknownFloat11;
};

struct PacketC : public PacketTilda {
    char surfaceType[4];
    int32_t currentLap;
    float wheelSteeringAngle[2];
    float wheelBase;
    char carCategory[4];
};

#pragma pack(pop)

static_assert(sizeof(PacketA) == 296, "GT7 packet A layout changed");
static_assert(sizeof(PacketB) == 316, "GT7 packet B layout changed");
static_assert(sizeof(PacketTilda) == 344, "GT7 packet tilde layout changed");
static_assert(sizeof(PacketC) == 368, "GT7 packet C layout changed");
constexpr int32_t kGt7Magic = 0x47375330;
constexpr std::size_t kPacketABytes = sizeof(PacketA);
constexpr std::size_t kPacketCBytes = sizeof(PacketC);
