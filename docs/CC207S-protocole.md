# CC207S-V2.0 bus protocol — byte map, test methodology and field notes

Reverse-engineered between 22 Aug and 31 Oct 2026 on a **Turcotte TTi ECOLO 50**
pool heat pump (controller board: Guangdong Chico Electronic **CC207S-V2.0**).
The same controller and keypad protocol are reported on other rebadged heat
pumps (Hayward and others, other CC207S versions — probably compatible, untested) but **only the unit above
has been tested** by the author. Treat everything below as "true on one unit".

> The file name is French (`protocole`) for historical reasons — the component's
> source comments point at it by that name. The content is English.

Contents

1. [Physical layer](#1-physical-layer)
2. [Frame format and checksum](#2-frame-format-and-checksum)
3. [`0xDD` telemetry frame (controller → keypad)](#3-0xdd-telemetry-frame)
4. [`0xD2` / `0xCC` configuration and command frame](#4-0xd2--0xcc-configuration-and-command-frame)
5. [Fault codes](#5-fault-codes)
6. [The P3 / P4 / P5 / P7 investigation (Oct 2026)](#6-the-p3--p4--p5--p7-investigation-oct-2026)
7. [Known controller behaviour](#7-known-controller-behaviour)
8. [How the bits were confirmed](#8-how-the-bits-were-confirmed)
9. [Open questions](#9-open-questions)
10. Extra field notes
11. [Other brands and models (unverified)](#11-other-brands-and-models-unverified)

---

## 1. Physical layer

* The controller board, the keypad and (in this project) the ESP32 share
  **one half-duplex data line plus ground**. The keypad is a full participant on
  that wire: it both displays what the controller sends and transmits commands.
* Signalling is slow, roughly **500 baud**. The component does not use a UART:
  it samples the line with the ESP32 **RMT** peripheral (312.5 kHz tick,
  `hwp_rmt_adapter.h`) and decodes pulse widths in `Decoder.cpp`.
  The bus/RMT/decoder layer comes unchanged from
  [sle118/hayward_pool_heater](https://github.com/sle118/hayward_pool_heater)
  (MIT), which targets a related Hayward protocol.
* One GPIO (`pin_txrx` in YAML) is used for both directions.
* Electrical details of the interface and the pitfalls hit in the field are in
  [`wiring.md`](wiring.md).

## 2. Frame format and checksum

Every frame is **14 bytes**:

```
byte 0        byte 1 .. 12      byte 13
type          payload d1..d12   checksum
```

In the source, `packet.data[0]` is the type byte and `d1`..`d12` are
`packet.data[1]`..`packet.data[12]`.

Checksum = sum of bytes 1..12, modulo 256. (`protocol_core.cpp` adds a further
`+0x0B` for types whose top two bits are `00`; none of the three CC207S frame
types — `0xDD`, `0xD2`, `0xCC` — are affected.) Validated against captured
frames: 97.8 % of the 90 frames of the first capture matched.

| Type  | Direction                     | Rate                | Meaning                          |
|-------|-------------------------------|---------------------|----------------------------------|
| `0xDD`| controller → keypad           | about every 6 s     | live telemetry (temperatures, relays, faults) |
| `0xD2`| controller → keypad           | about every 6 s     | stored configuration re-broadcast (setpoint, power) |
| `0xCC`| keypad → controller           | one burst per key press | command: same payload layout as `0xD2` |

A 16-minute idle capture contained 164 `0xD2` frames and **zero** `0xCC` frames:
the keypad only transmits when a button is pressed.

## 3. `0xDD` telemetry frame

All temperatures are an unsigned byte, **`raw / 2` = °C** (0.5 °C resolution).
Because the byte is unsigned, anything at or below 0 °C reads `0.0` — the
sensors never report negative values on the bus.

| Byte | Content | Notes |
|------|---------|-------|
| d1   | Water temperature | the only sensor calibrated against known points (83 °F and 46 °F) |
| d2   | Compressor discharge temperature | |
| d3   | Defrost (coil) temperature | |
| d4   | Suction temperature | physically wired to input SEN5 |
| d5   | Ambient air temperature | physically wired to input SEN4 |
| d6   | secondary fault register | inconsistent, **unused** |
| d7   | **blocking** fault bitfield | see [section 5](#5-fault-codes) |
| d8   | compressor run indicator | `0x23` = off, `0x0A` = on |
| d9   | **sensor** fault bitfield | `0x04` P3, `0x08` P4, `0x10` P5 |
| d10  | relay bitfield | `0x01` crankcase heater, `0x02` compressor, `0x08` recirculation pump, `0x10` fan |
| d11  | PORT1 input | bit `0x02` set = contact open |
| d12  | water temperature, whole °F | what the keypad displays; used only as a cross-check |

Sensor-to-byte order was established by substitution tests (see
[section 8](#8-how-the-bits-were-confirmed)). Only the water sensor was
calibrated against known reference points; the other four use the same
`raw / 2` formula by analogy. The ambient reading tracks the real outdoor
temperature plausibly, but it has not been formally calibrated.

**Open-circuit sentinels.** A disconnected thermistor reads `0x28` (= 20.0 °C)
on SEN1/3/4/5, and discharge reads `0x00`. A genuine reading can coincide with
these values, so they are only logged as hints (`water-open-circuit`,
`discharge-open-circuit`), never used alone to raise a fault.

## 4. `0xD2` / `0xCC` configuration and command frame

`0xD2` is the controller re-broadcasting the configuration it keeps in its own
24C02 EEPROM. `0xCC` has **exactly the same 12-byte payload and checksum** — only
the type byte differs. Captured proof:

```
17:48:03  CC  18 3C 3C 08 0E 3C 7E 06 14 50 01 00  cs=CB
17:48:05  D2  18 3C 3C 08 0E 3C 7E 06 14 50 01 00  cs=CB
```

| Byte | Content |
|------|---------|
| d2   | **setpoint**, `raw / 2` = °C (`0x3C` = 60 → 30.0 °C → 86 °F) |
| d7   | bit `0x40` = **power** (ON = `0x7E`, OFF = `0x3E`) |
| others | constant in every capture: installer parameters (limits, defrost thresholds, timers). `d3` and `d6` also decode to 30.0 °C and may be setpoint clamps — unverified, not used |

The setpoint accepted by the component is limited to 15–33 °C in 0.5 °C steps
(the controller's own resolution).

### Injecting a command

The component builds a `0xCC` frame from the last `0xD2` payload, changes only
the fields that were requested (setpoint and/or the power bit), recomputes the
checksum and transmits it in parallel on the shared bus. No man-in-the-middle
bridge is needed because the keypad is silent unless a button is pressed.

* **Verification is free**: watch the next `0xD2`. If it does not carry the new
  value, the unit did not accept the command and nothing changed.
* **Latency**: the controller echoes the new value in `0xD2` after roughly
  **4–10 s** (about 8 s typical). Anything that re-reads the setpoint inside
  that window sees the old value — the YAML example has a grace period for
  exactly this reason.
* The component waits up to **15 s** for the echo, then raises the
  `command_not_confirmed` diagnostic. It does not retry on its own.
* A person pressing a keypad button afterwards simply overwrites the injected
  command, which is the intended behaviour.
* The component refuses to build a command until it has received at least one
  `0xD2` (it needs the current payload as a template).

## 5. Fault codes

Decoded from `d7` (blocking) and `d9` (sensor). The text published by the
`fault_codes` sensor is `OK` or a space-separated list, e.g.
`P3 (defrost sensor) P5 (ambient sensor)`.

| Code | Register / bit | Meaning | Blocks the unit? |
|------|----------------|---------|------------------|
| P1   | d7 `0x01` | water sensor | yes — cannot regulate |
| P2   | d7 `0x02` | discharge sensor | yes — compressor protection; raised only while the compressor runs/starts (idle disconnect produces d2 = `0x00` but no flag) |
| E3   | d7 `0x04` | water flow | yes |
| E4   | d7 `0x08` | over-current (3-phase input, PORT2) | yes; factory-jumpered on single-phase units |
| E1   | d7 `0x20` | high refrigerant pressure | yes |
| E2   | d7 `0x40` | low refrigerant pressure | yes |
| P3   | d9 `0x04` | defrost sensor | no (confirmed: unit starts and heats with P3 showing) |
| P4   | d9 `0x08` | suction sensor | no (same) |
| P5   | d9 `0x10` | ambient air sensor | **unknown** — see section 6 |
| P7   | *none*    | keypad-only display, see section 6 | — |
| E6   | *not decoded* | compressor discharge overload (listed in the manufacturer manual) | presumably yes |

Unmapped bits `d7 0x10`, `d7 0x80`, `d9 0xE2` and `d10 0xC4` raise the
`unknown_bits_detected` diagnostic so they are not silently dropped. `d7 0x10`
is the most likely home of E6 but has never been observed with a real E6.

Manufacturer manual list (translated from the French original): E1 high
refrigerant pressure; E2 low refrigerant pressure; E3 low or missing water
pressure; E4 current overload (3-phase units); E6 compressor discharge overload;
P1 pool/spa water sensor fault; P2 discharge sensor fault; P3 coil sensor fault;
P4 inlet-pipe sensor fault; P5 air temperature sensor fault; P7 "automatic stop
(non-fault) when the temperature is below 7 °C / 45 °F".

## 6. The P3 / P4 / P5 / P7 investigation (Oct 2026)

Datasets: Home Assistant history exports covering 20–31 Oct 2026 (nights near
freezing, days 7–15 °C), the ESPHome debug log of 23 Oct, and a photo of the
keypad. The unit was idle (no heat demand) during almost all of it.

### 6.1 P7 is drawn by the keypad, not sent by the controller

On 23 Oct the keypad displayed **`P7`** with a periodic beep. At that moment the
log showed `d7 = 0x00` and `d9 = 0x14` (P3 + P5), ambient 1.5 °C, compressor
relay off. No bus byte changes for P7 anywhere, so the keypad produces it on
its own.

### 6.2 P7 is not "two sensor faults at once"

A first hypothesis (any two of P3/P4/P5 → P7) was falsified: on many cold
mornings (ambient 3–5 °C) the author saw P3, P4 and P3 + P4, and — from his
recollection, no logs kept — the heat pump started and heated normally. The only flag present during the P7 sighting that
was absent in those runs is **P5**. Whether P5 is what makes the keypad show P7
(and refuse to start) is **not confirmed**: nobody sent a start command while P5
was latched. The test is listed in [section 9](#9-open-questions).

An earlier memory of unplugging the ambient probe (SEN4) while running — the
keypad showing a bogus reading and then P5 with a stop — is plausible but
**undocumented**; no logs survive.

### 6.3 P3 / P4 / P5 are latches on the 0.0 °C floor

For each of the three flags, every logged event (about 47 between 22 and 31 Oct,
no exception except one 1-second reconnect glitch) follows the same rule, tied to
the flag's **own sensor** (P3 ↔ defrost d3, P4 ↔ suction d4, P5 ↔ ambient d5):

* **set** when that sensor's reading goes from `0.0` to `0.5` °C;
* **cleared** when that sensor's reading is back at `0.0` °C.

Worked example (local time, ambient / fault code):

| Time | Ambient | Event |
|------|---------|-------|
| 20:27 | 0.0 °C | code is `OK` |
| 20:47 | 0.0 → 0.5 °C | **P5 appears** |
| 21:28 | defrost 0.0 → 0.5 °C | **P3 appears** |
| 01:50 | suction 0.0 → 0.5 °C | **P4 appears** |
| 01:50–07:20 | 2.0–3.5 °C | P3 + P4 + P5 stay set |
| 07:45 | suction back to 0.0 | P4 clears |
| 08:39 | ambient back to 0.0 °C | P5 clears |
| 08:42 | defrost back to 0.0 °C | P3 clears, `OK` all day |

Consequences:

* The flags are **not** "temperature below X". They persisted ~12 h at ambient
  up to 3.5 °C and cleared at the coldest point of the morning.
* The manual's 7 °C threshold does not show up on the bus at all; the flags
  appear and clear around 0 °C.
* Water temperature plays no role. A first suspicion (ambient + water) was ruled
  out with the filter pump running continuously, which removed a stagnant-pipe
  artefact from the pool-water probe: water stayed 6.5–9.5 °C while the flags
  followed only their own sensor.
* Open puzzle: the same 0.0 → 0.5 °C tick at sunrise did **not** set the flag
  again. Something else (time spent at 0.0, an internal timer) is probably part
  of the rule.
* Practical advice: to know whether P5 is active, watch the `fault_codes` sensor
  — do not infer it from the ambient temperature. And expect these codes on
  every freezing night even with the heat pump switched off for the winter;
  mute notifications on that sensor.

## 7. Known controller behaviour

* **Power OFF over the bus stops the compressor only.** Fan, recirculation pump
  and crankcase heater keep running, exactly like the keypad's own OFF key.
  Components that need more (for example stopping the filter pump) must do it
  from outside.
* **Anti-short-cycle.** On 19 Sept the compressor relay stayed off for several
  minutes despite heat demand after an OFF/ON/OFF/ON sequence. It is the
  controller's own protection, not a fault; no flag is raised.
* **E3 follows real water flow, not the bus mode.** On 23 Sept E3 cleared about
  8 s after the physical filter-pump switch was turned back on, while the heat
  pump's own bus mode was still OFF the whole time. An earlier theory of an
  internal ~56 s debounce timer was wrong.
* **Compressor running without compressing.** There is no protocol fault for it.
  A failing start capacitor produced audible buzzing and a start/stop retry loop
  with all of E1–E4 and P1–P5 clear. The component derives its own diagnostic:
  relay on for more than 90 s while `discharge − suction` stays under 15 °C
  (reference for a healthy compressor on this unit: > 20 °C within 60 s and
  > 50 °C within 5 min).
* **Bus watchdog.** If no frame is seen for 60 s the `bus_communication_lost`
  diagnostic fires. Added after a soldered bus wire snapped in a test enclosure
  and the ESP kept showing stale values for hours.
* **Which "source" field gets filled depends on wiring.** The decoder labels
  frames heater/controller from the signal polarity it detects, so on some
  installations only one of the two timestamps is updated even though telemetry
  flows fine; the watchdog therefore uses whichever is more recent.
* **Default mode.** The component starts in HEAT until the first `0xD2` arrives,
  otherwise the `action` would stay OFF after every reboot.

## 8. How the bits were confirmed

* **Substitution tests**: disconnect a sensor, wait for the frame to change,
  then substitute a 10 kΩ resistor. This fixed the sensor-to-byte order.
* **Contact tests**: open and close an input (PORT1, flow) and watch `d7`/`d11`.
* **Keypad presses**: step the setpoint up and down (6 increments and 9
  decrements confirmed byte `d2`), press Power and watch `d7 0x40`.
* **Manual cross-check** for the code names.
* Rule applied throughout: a bit is only decoded once it has been confirmed by
  one of the methods above (a provoked fault, a real occurrence, or the manual's
  list matched to an observed bit). That is why E6 is not decoded.

## 9. Open questions

1. **Does P5 stop or block the heat pump?** Latch P5 on a cold night (ambient
   2–3 °C is enough once it is set), switch the filter pump on for 1–2 minutes,
   send `mode: heat` with a setpoint well above the water temperature, and log
   `d7`, `d9` and the compressor relay (`d10 & 0x02`). The 23 Oct attempt had the
   relay off throughout, but P3 + P5 were both set and there is no clean
   P5-only test.
2. Why does the 0.0 → 0.5 °C tick not always set the flag (daytime vs night)?
3. Which bit carries E6?
4. What did the keypad's bogus reading (about −45) mean when a probe was
   unplugged, and was it only a keypad-side display?
5. Do other CC207S-based pumps (Hayward, other V2.x boards) use the same byte map?

## 10. Extra field notes (author's first write-up, 22–23 Aug 2026)

Hardware identification: board CC207S-V2.0 (Guangdong Chico), label HL318029
(2020-09-21), firmware `THLCH15.HEX <5883>`, matched keypad HL318030
(2020-09-22); 208–240 V / 1 ph / 60 Hz, heat only, no reversing valve. The
keypad is reused across brands, so other CC207S units may share the frame
format (unverified).

* **`0x22` frames** are `0xDD` received with inverted polarity and truncated
  (`0x22 = ~0xDD`): a decoder artefact, discard them.
* **P2 is only evaluated while the compressor runs**; unplugging SEN2 with the
  compressor stopped sets no bit and shows no code.
* **LP (E2) is bypassed for about 3 minutes after compressor start**; opening it
  later trips instantly. (An early test wrongly concluded LP was not monitored.)
* **PORT2 (E4)** is factory-jumpered on this single-phase unit; the bit is only
  an over-current input on three-phase models, and tripping here would mean the
  jumper failed.
* **Anti-short-cycle:** 3 min 02 s measured between compressor stop and restart.
* **Not on the bus:** keypad schedule/RTC (changing the start time sends no
  frame; the keypad just sends a power command at the set time), and the
  Hayward PC1000/PC1001 `CONFIG_1`…`CONFIG_6` frames — which is why many
  entities of the upstream component stay empty on this board.
* **PORT1 lockout:** after roughly 10–20 h with PORT1 open the keypad beeps and
  asks for a power cycle; no counter is broadcast (10.5 min of opening changed
  nothing on the bus).
* **EEPROM wear:** every injection writes the controller's 24C02; write on real
  change only, never as a periodic refresh.
* **`d6`** mirrors sensor faults about 6 s late with an arbitrary bit order
  (P3 `0x20`, P4 `0x40`, P5 `0x10`) plus a `0x80` bit that does not clear on
  reconnect; use `d9`.
* The first write-up called `d8` an undecoded compressor-linked value that
  "resets to `0x23` after a fault clears"; later work treats it as a
  run indicator (`0x23` off, `0x0A` on) — see section 3.

### Sensor connectors (author's unit; confirmed both directions in the first write-up)

The terminal numbering on the board does **not** match the protocol numbering
for the last two probes: `SEN4` is the ambient probe and `SEN5` the suction
probe. A **P4** code therefore points at the probe plugged on **SEN5**, not
SEN4.

| Connector | Wire colour | Probe | Byte | Code |
|-----------|-------------|-------|------|------|
| SEN1 | red   | Water     | d1 | P1 |
| SEN2 | white | Discharge | d2 | P2 |
| SEN3 | blue  | Defrost   | d3 | P3 |
| SEN4 | black | Ambient air | d5 | P5 |
| SEN5 | green | Suction   | d4 | P4 |

More details from that write-up:

* **Timing:** `0xDD` and `0xD2` each repeat every 6.05 s, alternating about
  3.02 s apart. A 16-minute idle capture held 164 `0xD2` frames and no `0xCC`.
* **P1** (water probe) also sets `d9` bit `0x01`, together with bit `0x20`.
* **`d10` values observed:** `0x00` all off, `0x08` pump only, `0x18` fan,
  `0x1A` fan + compressor. The pump bit drops about 25 s after the fan stops.
* **Temperature cross-check:** against independent DS18B20 probes the board read
  27.5 °C water / 23.0 °C air while the external probes read 27.1 °C / 23.9 °C.
  The board's ambient reading runs slightly low, consistent with a probe in the
  evaporator airstream.
* **Keypad display:** the keypad shows whole °F, while the controller works in
  0.5 °C steps, so one button press moves the raw setpoint by 1 or 2 units.
* **Open items from that write-up:** the ten constant configuration bytes, the
  PORT1 lockout error code and `PORT3`–`PORT7` (connectors left unpopulated on
  the whole CC207S range).

### Relay outputs and board revisions

Relay labels printed on the board (the same layout appears on CC207 boards of
other revisions, e.g. a CC207-V4.4 from 2012, which also has an `SEN6` header and
`PORT`/`PROT` inputs 1–7). Which `d10` bit drives which relay was only verified
for the four rows marked *observed*:

| Relay | Output | Function | `d10` bit |
|-------|--------|----------|-----------|
| RY1 | OUT1 | compressor (contactor) | `0x02` (observed) |
| RY2 | OUT2 | pump | `0x08` (observed) |
| RY3 | OUT3 | SPA (optional) | not observed |
| RY4 | OUT4 | 4-way reversing valve (hot/cold; not fitted on a heat-only unit) | not observed |
| RY5 | OUT5 | high-speed fan | `0x10` (observed, fan) |
| RY6 | OUT6 | low-speed fan | not observed |
| RY7 | OUT7 | crankcase heater | `0x01` (observed) |

The manufacturer's wiring diagram for the horizontal 50/65 model (not reproduced
here) is consistent with the rest of this document: the five sensor connectors
`SEN1`–`SEN5` are red (water), white (discharge), blue (defrost), black (air) and
green (suction), the keypad cable is blue / yellow / brown, and the board has
separate inputs for the flow switch (FS, `E3`), low-pressure (LP, `E2`) and
high-pressure (HP, `E1`) switches, a transformer in/out pair (CN2/CN8) and a 5 A
fuse. Whether other board generations (V4.4, V2.1, V2.3) speak the same bus
protocol has **not** been checked.

### Keypad board (author's unit)

The keypad (label `HL318030`) contains a board marked **CC343C-V2.1**
(`W181204`). What is visible on the board (photos not included):

* **Front:** a 4-digit seven-segment display, push-buttons `K1`–`K8` (`K3` and
  `K4` footprints are empty), LEDs `LD1`–`LD8`, a buzzer (`BUZ`), a coin-cell
  backup battery (`BT`, with "2032" printed beside it), an optocoupler (`IC2`),
  a 78L05-type regulator (`IC1`) and a diode (`D1`). A 3-pad footprint labelled
  `RCV` / `OUT` `GND` `VCC` (an infrared receiver position) and a 5-pad header
  labelled `SDA`, `SCL`, `CN1`, `VCC`, `GND` are left unpopulated.
* **Back:** the 3-pin connector `CN3` where the blue / yellow / brown cable
  lands, a microcontroller `IC3` with the label `THL35GXK25.HEX <5D8E>`, a
  16-pin chip (`IC5`), an 8-pin chip (`IC6`), transistors `Q1`–`Q7` and an empty
  `IC7` footprint.

What this suggests, without proving it: the keypad has its own microcontroller
and a backed-up clock, which fits the finding that the schedule and the clock
live in the keypad and not on the bus. The optocoupler and transistors are
consistent with an isolated receive path and an open-collector transmit path on
the single data wire. The controller firmware label (`THLCH15.HEX <5883>`) and
this one share the same `THL…` naming. No datasheet or schematic was found, and
the identity of `IC5` and `IC6` is unknown.

## 11. Other brands and models (UNVERIFIED)

**Nothing in this section has been tested.** Only the Turcotte TTi ECOLO 50 with a
CC207S-V2.0 board has been captured and decoded. What follows is circumstantial
evidence that the same controller family sits in many more machines than one
Quebec brand. Visual similarity of a keypad is a lead, not proof of the same
bus protocol.

* **The keypad is the easiest way to recognise the family.** The same
  white keypad, with a small display, up / down keys, three timer keys, a
  power key and a HEAT lamp, is seen on the author's TTi and on other brands.
  Variants exist with HEAT / COOL lamps. Their footprint and connector layout
  look almost the same.
* **Jacuzzi** swimming-pool heat pumps JA50 / JA65 / JA85 (sold in Quebec by Club
  Piscine): the manual (not reproduced here) shows a main control board with the
  same outputs `OUT1`–`OUT7`, a `Wire controller` on a 3-wire connector, a signal
  cable allowed up to 50 m (AWG 20), and the same fault-code set:
  `P1` water inlet sensor, `P2` coil (cooling), `P3` coil (heating),
  `P4` compressor return gas, `P5` ambient, `P7` low ambient protection,
  `E1` high pressure, `E2` low pressure, `E3` water flow, `E4` power / phase loss,
  `E8` controller communication failure, defrost lamp flashing. This matches the
  bit map in section 5, and supports the finding that `P7` is a keypad-side
  protection with no bus bit (section 6.1). `E8` is probably the keypad losing the
  bus; it is not a telemetry bit.
* **Hayward** (HP50A-type keypads) and similar keypads: same look and same
  family of controller, protocol not compared.
* **Fairland / Australia:** a public post (Pool Owners Australia group, May 2025)
  shows a keypad of the same style with a HEAT lamp, whose back carries the
  maker's marking **CHICO** and a date code (2014-12), on a **Fairland** heat pump,
  with the owner saying it was used on other brands too. This suggests the
  Guangdong Chico controller is an OEM part fitted to many brands, sold in
  Australia as well as in North America, and has been in production for years.
* **Consequence:** the brand on the machine tells little. What counts is the
  controller (`CC207S` and its version) and the keypad. Different versions
  (the author has seen photos of other `CC207S V x.x` boards with the same relay
  layout) may use the same frames or may not.

**How to help:** if you own one of these machines, capture the bus (see section 1
for the wiring and `examples/heatpump.yaml` with logging) and open an issue with the
board label, the keypad photo, the heat pump brand and a few `0xDD` / `0xD2`
frames. Matching checksums on a frame of 14 bytes with the same layout would be
the first real evidence.

