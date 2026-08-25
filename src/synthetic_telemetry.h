// synthetic_telemetry.h
//
// Generates synchronized, continuous physics-based synthetic telemetry
// for the 4 surveillance domains (Terrestrial, Airtime-Maritime, Replay-DoS, GNSS)
// on System 2 and packages them for streaming to System 3.

#pragma once

#include <cmath>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <vector>

#include "net_protocol.h"

enum class ThreatScenario
{
    kNormal = 0,
    kGnssChirp = 1,
    kGnssSpoof = 2,
    kDosFlood = 3,
    kReplayAttack = 4,
    kEwSurge = 5
};

class SyntheticTelemetryEngine
{
public:
    SyntheticTelemetryEngine();

    void set_scenario(ThreatScenario scenario);
    ThreatScenario scenario() const;

    // Advances kinematic positions, threat scores, and spectra; populates out_telem
    void update(double dt_sec, ExtendedDomainTelemetry& out_telem);

private:
    void init_contacts();
    void init_gnss_sats();
    void init_modbus_regs();

    mutable std::mutex mutex_;
    ThreatScenario scenario_{ThreatScenario::kNormal};
    uint64_t tick_count_{0};

    ExtendedDomainTelemetry state_{};
};
