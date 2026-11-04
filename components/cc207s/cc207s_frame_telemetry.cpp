/**
 * CC207S-V2.0 -- 0xDD telemetry frame parsing.
 * Replaces FrameConditionsD.cpp. Logic is unchanged from the validated
 * version (22-30 Aug 2026 field testing); only the surrounding class
 * structure was simplified -- see cc207s_frame_telemetry.h for why no typed
 * struct or CLASS_DEFAULT_IMPL macro is needed.
 */
#include "cc207s_frame_telemetry.h"
#include <cinttypes>

namespace esphome {
namespace hwp {

static constexpr const char* TAG = "cc207s";

std::shared_ptr<BaseFrame> CC207SFrameTelemetry::create() {
    return std::make_shared<CC207SFrameTelemetry>();
}

bool CC207SFrameTelemetry::matches(BaseFrame& specialized, BaseFrame& base) {
    return base.packet.get_type() == FRAME_ID && base.size() == frame_data_length;
}

void CC207SFrameTelemetry::traits(climate::ClimateTraits& traits, heat_pump_data_t& hp_data) {
    traits.add_feature_flags(
        climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
}

/**
 * CC207S-V2.0 byte map (packet.data[N], N=1..12, N=0 is the 0xDD type byte):
 *
 *   d1  Water        raw/2 = degC
 *   d2  Discharge     raw/2 = degC
 *   d3  Defrost       raw/2 = degC
 *   d4  Suction       raw/2 = degC   (physically wired to SEN5)
 *   d5  Ambient air   raw/2 = degC   (physically wired to SEN4)
 *   d6  secondary fault register -- inconsistent bit assignment, unused
 *   d7  BLOCKING faults bitfield -- see table below
 *   d8  compressor run indicator: 0x23(35)=off, 0x0A(10)=on
 *   d9  NON-BLOCKING sensor fault bitfield -- see table below
 *   d10 relay bitfield: 0x01 crankcase heater, 0x02 compressor,
 *       0x08 recirc pump, 0x10 fan
 *   d11 PORT1 input, bit 0x02 = open
 *   d12 water temp, whole degF (keypad display cross-check only)
 *
 * Full rationale, test dates and manufacturer error-code cross-reference:
 * see docs/CC207S-protocole.md in this repository.
 */
void CC207SFrameTelemetry::parse(heat_pump_data_t& hp_data) {
    const uint8_t* d = this->packet.data;

    // ---- temperatures ------------------------------------------------------
    float water_temp_c = d[1] / 2.0f;
    float discharge_temp_c = d[2] / 2.0f;
    float defrost_temp_c = d[3] / 2.0f;
    float suction_temp_c = d[4] / 2.0f;
    float ambient_temp_c = d[5] / 2.0f;

    hp_data.current_temperature = water_temp_c;
    hp_data.t_discharge = discharge_temp_c;
    hp_data.t_defrost = defrost_temp_c;
    hp_data.t_suction = suction_temp_c;
    hp_data.t_ambient = ambient_temp_c;

    ESP_LOGD(TAG,
        "CC207S-TEMP: water=%.1fC discharge=%.1fC defrost=%.1fC suction=%.1fC air=%.1fC",
        water_temp_c, discharge_temp_c, defrost_temp_c, suction_temp_c, ambient_temp_c);

    // ---- fault registers -----------------------------------------------
    // d7 BLOCKING: the controller stops the unit. d9 SENSOR faults: P3 and P4
    // are reported while the unit keeps running (confirmed: it started and
    // heated normally with P3, P4 and P3+P4 showing). P5 is the odd one out --
    // see the long note below the table. Every bit was confirmed by physical
    // testing (sensor disconnect, contact open, manual/manufacturer
    // error-code cross-check).
    uint8_t blocking = d[7];
    uint8_t sensor_faults = d[9];

    std::string codes;
    auto add = [&codes](const char* c) {
        if (!codes.empty()) codes += " ";
        codes += c;
    };
    // Each code carries a short English description in the published string
    // itself (not just in this source comment) -- e.g. "P1 (water sensor)"
    // -- so the fault_codes text_sensor is self-explanatory in Home
    // Assistant's history/logbook without needing a lookup table.
    if (blocking & 0x01) add("P1 (water sensor)");        // unit cannot regulate
    if (blocking & 0x02) add("P2 (discharge sensor)");    // compressor protection
    if (blocking & 0x04) add("E3 (water flow)");
    if (blocking & 0x08) add("E4 (over-current)");        // PORT2, 3-phase; factory-jumpered here
    if (blocking & 0x20) add("E1 (high pressure)");
    if (blocking & 0x40) add("E2 (low pressure)");
    if (sensor_faults & 0x04) add("P3 (defrost sensor)");
    if (sensor_faults & 0x08) add("P4 (suction sensor)");
    if (sensor_faults & 0x10) add("P5 (ambient sensor)");

    // ---- "P7" on the physical keypad -------------------------------------
    // The manufacturer manual lists P7 as "Arret automatique (NON-
    // DEFECTUEUX) lorsque la temperature est sous 7C ou 45F" (automatic,
    // non-fault stop below 7C / 45F). What we actually measured (Oct 2026,
    // one frozen-morning observation plus ~47 sensor-fault events logged
    // through Home Assistant between 22 and 31 Oct):
    //
    //  1. P7 has NO bit on the bus. While the keypad displayed "P7" (photo
    //     confirmed, 23 Oct) d7 read 0x00 and d9 read 0x14 (P3 + P5). The
    //     keypad draws "P7" on its own; there is nothing to decode here.
    //
    //  2. P7 is not "any two of P3/P4/P5". An earlier version of this
    //     comment claimed that from the single observation above; it was
    //     wrong. P3, P4 and P3+P4 were seen many times on cold mornings
    //     (3-5C ambient) while the unit started and heated normally and no
    //     P7 appeared. The only fault flag present in the one P7 sighting
    //     that was absent in those runs is P5 (ambient sensor), so P5 is
    //     the prime suspect -- but a start attempt with P5 latched has not
    //     been tested yet, so treat "P5 => P7 / refuses to start" as an
    //     unconfirmed hypothesis (the heat pump never actually ran during
    //     the sighting: compressor relay stayed off).
    //
    //  3. P3, P4 and P5 are NOT "temperature below X" flags. For all three,
    //     in every logged event, the flag behaves like a latch tied to its
    //     own sensor reading hitting the 0.0C floor (raw byte 0x00 -- the
    //     byte is unsigned, so 0.0 means "0C or colder"):
    //         set    when that sensor's reading ticks up from 0.0 to 0.5C
    //         clears when that sensor's reading returns to 0.0C
    //     Example: on a frosty night P5 appeared at 20:47 when ambient went
    //     0.0 -> 0.5C, stayed set for 12 hours while ambient climbed to
    //     3.5C, and cleared at 08:39 when ambient dropped back to 0.0C.
    //     Consequently the flags can persist for hours at ambient
    //     temperatures well above freezing, and a Home Assistant rule such
    //     as "ambient < 7C" does NOT predict them. Watch fault_codes itself.
    //     Unexplained: the same 0.0 -> 0.5C tick at sunrise does not set the
    //     flag again, so some extra condition (time spent at 0.0C?) is
    //     probably involved.
    //
    //  4. The manual's 7C figure does not match anything seen on the bus:
    //     the flags appeared and cleared around 0C, never near 7C.
    //
    // Also from the manual: our blocking-fault table above is missing
    // "E6 (compressor discharge overload)". The only still-unmapped bits in
    // d7 after P1/P2/E3/E4/E1/E2 are 0x10 and 0x80; 0x10 is a plausible
    // candidate for E6 but has not been confirmed by a real occurrence, so
    // it is deliberately left undecoded (and trips the "unknown bit"
    // diagnostic below) rather than guessed at.

    // Open-circuit sentinels: 0x28 (=20.0C) on SEN1/3/4/5, 0x00 on discharge.
    // Never rely on these alone -- a genuine reading can coincide.
    bool sentinel_water = (d[1] == 0x28);
    bool sentinel_discharge = (d[2] == 0x00);

    uint8_t unmapped_blocking = blocking & ~0x6F;
    uint8_t unmapped_sensor = sensor_faults & ~0x3D;
    if (blocking != 0x00 || sensor_faults != 0x00) {
        ESP_LOGW(TAG, "CC207S-FAULT: %s | d7=0x%02X d9=0x%02X%s%s%s%s",
            codes.empty() ? "(bits set but unmapped)" : codes.c_str(), blocking, sensor_faults,
            unmapped_blocking ? " +unmapped[7]" : "", unmapped_sensor ? " +unmapped[9]" : "",
            sentinel_water ? " water-open-circuit" : "",
            sentinel_discharge ? " discharge-open-circuit" : "");
    }
    hp_data.fault_codes = codes;
    hp_data.raw_faults_blocking = blocking;
    hp_data.raw_faults_sensor = sensor_faults;

    // ---- unknown-bit sentinel -----------------------------------------
    // Flags a bit this decode never saw (Heat/Cool 4-way valve, 2nd fan
    // speed, etc.) instead of silently dropping it -- useful for anyone
    // running this on a differently-configured CC207S unit.
    uint8_t unknown_d7 = blocking & 0x90;
    uint8_t unknown_d9 = sensor_faults & 0xE2;
    uint8_t unknown_d10 = d[10] & 0xC4;
    bool has_unknown_bits = (unknown_d7 || unknown_d9 || unknown_d10);
    hp_data.unknown_bits_detected = has_unknown_bits;
    if (has_unknown_bits) {
        ESP_LOGW(TAG,
            "CC207S-UNKNOWN: unseen bit(s) -- d7=0x%02X(unmapped=0x%02X) d9=0x%02X(unmapped=0x%02X) "
            "d10=0x%02X(unmapped=0x%02X). Possibly a function (4-way valve, 2nd fan speed) not "
            "present on the unit this was decoded against.",
            blocking, unknown_d7, sensor_faults, unknown_d9, d[10], unknown_d10);
    }

    // ---- PORT1, relays ---------------------------------------------------
    hp_data.port1_open = (d[11] & 0x02) != 0;

    uint8_t relays = d[10];
    hp_data.raw_relays = relays;
    bool compressor_on = (relays & 0x02) != 0;
    bool fan_on = (relays & 0x10) != 0;
    hp_data.compressor_on = compressor_on;
    hp_data.fan_on = fan_on;
    hp_data.pump_on = (relays & 0x08) != 0;
    hp_data.crankcase_heater_on = (relays & 0x01) != 0;

    if (compressor_on) {
        hp_data.action = climate::CLIMATE_ACTION_HEATING;
    } else if (fan_on) {
        hp_data.action = climate::CLIMATE_ACTION_IDLE;
    } else {
        hp_data.action = climate::CLIMATE_ACTION_OFF;
    }

    // ---- compressor-running-without-compression detection -----------------
    // The controller only sees its own relay outputs and temperature sensors
    // -- no current sensing, no speed feedback. A failing start capacitor, a
    // seized compressor, or a lost refrigerant charge all look the same on
    // the bus: relay bit set, but discharge and suction stay close together
    // instead of diverging. No protocol fault code covers this condition.
    //
    // Confirmed in the field (a failing start capacitor): audible start
    // buzzing, a start/stop retry loop, and E1..E4/P1..P5 all stayed clear
    // throughout.
    //
    // NOT covered by this check (observed 19 Sept 2026): the relay bit can
    // also simply stay OFF for several minutes despite an active heat
    // demand and a setpoint well above water temperature, with power
    // toggled OFF/ON/OFF/ON shortly before -- almost certainly the
    // controller's own anti-short-cycle protection, not a fault. This
    // block never sees that case, since it only runs while compressor_on
    // is true.
    //
    // Reference (healthy compressor, this unit, 30 Aug 2026): discharge minus
    // suction exceeded 20C within 60s of the relay turning on, past 50C
    // within 5 minutes. The threshold/delay below are deliberately
    // conservative relative to that reference.
    static uint32_t compressor_on_since_ms = 0;
    const uint32_t COMPRESSION_CHECK_DELAY_MS = 90000;  // 90 s grace period
    const float COMPRESSION_MIN_DELTA_C = 15.0f;        // discharge - suction

    if (compressor_on) {
        if (compressor_on_since_ms == 0) {
            compressor_on_since_ms = millis();
        }
        uint32_t running_for_ms = millis() - compressor_on_since_ms;
        float compression_delta = discharge_temp_c - suction_temp_c;
        bool no_compression = (running_for_ms >= COMPRESSION_CHECK_DELAY_MS) &&
                               (compression_delta < COMPRESSION_MIN_DELTA_C);
        hp_data.compressor_not_compressing = no_compression;
        if (no_compression) {
            ESP_LOGW(TAG,
                "CC207S-DIAG: compressor relay ON for %" PRIu32 " ms but discharge-suction delta "
                "is only %.1fC (need %.1fC) -- compressor may not be turning (capacitor, seized "
                "motor, or lost charge). No protocol fault code covers this condition.",
                running_for_ms, compression_delta, COMPRESSION_MIN_DELTA_C);
        }
    } else {
        compressor_on_since_ms = 0;
        hp_data.compressor_not_compressing = false;
    }

    ESP_LOGD(TAG, "CC207S-STATE: relays=0x%02X comp=%d fan=%d pump=%d crankcase=%d | PORT1=%s",
        relays, compressor_on, fan_on, hp_data.pump_on.value_or(false),
        hp_data.crankcase_heater_on.value_or(false),
        hp_data.port1_open.value_or(false) ? "OPEN" : "closed");
}

}  // namespace hwp
}  // namespace esphome
