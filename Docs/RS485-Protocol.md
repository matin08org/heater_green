# RS485 application map v1 — board is the only master

Physical port: USART1 PB6 TX / PB7 RX through THVD1450DR. Proposed default is
19200 baud, 8 data bits, even parity, one stop bit (8E1); all endpoints must match. Master
operation is disabled until DE/RE wiring is supplied and confirmed in
`Assets/board.json`. The GPIO adapter expects DE and active-low /RE tied so
TX enables driver/disables receiver and RX does the opposite. THVD1450 pin3
is DE and pin2 is /RE; see [TI datasheet](https://www.ti.com/lit/ds/symlink/thvd1450.pdf).

Framing is Modbus RTU (little-endian CRC16 on the wire, big-endian register
bytes). See [Modbus specifications](https://www.modbus.org/modbus-specifications).
The 19200/8E1 interoperability default follows the serial-line guide's default
baud/parity options: [Modbus Serial Line V1.02](https://www.modbus.org/file/secure/modbusoverserial.pdf).
The register meanings below are **our application contract**, not an assertion
that an existing Delta program or commercial BH1750 adapter already uses them.
Slave configuration/firmware must be checked against this map.

Only this board initiates requests. PLC, lux adapter and optional PC debugger
are slaves with unique IDs. Slaves reply only to their own addressed request;
no unsolicited logs or multi-master traffic is permitted. A PC debugger must
not run as another master on the same wires.

The transceiver is currently NOT connected according to the board owner. The
engineering profile therefore keeps enabled=false; selecting a baud rate and
register map does not activate the bus or substitute for DE/RE wiring.

## Catalogue connection contract

| Parameter | Required value / meaning |
|---|---|
| Physical interface | Two-wire half-duplex RS485; THVD1450 on board |
| Serial framing | 19200 baud, 8E1; configurable, identical on every node |
| Board role | Only RTU master; board does not expose an RTU slave address |
| PLC role | RTU slave/server, default station1 |
| Lux adapter | RTU slave/server, station2; FC04, lux x100 |
| Optional debug receiver | RTU slave/server, station247; disabled by default |
| PLC functions | FC03 reads commands, FC16 writes status |
| Register addressing | Zero-based 16-bit PDU addresses, not 4xxxx notation |
| Multiword order | High16 first; each register high byte first |
| CRC | Modbus CRC16; low CRC byte first on wire |

Delta DVP14SS2 must be programmed/configured as the slave and its real D-register
mapping checked against its model/firmware manual. Addresses below are our wire
contract, not a promise that all PLCs expose the same native register names.
For LOGO!, the exact model and an appropriate gateway are required when only
Modbus TCP is available. Our fixed-master bus needs the gateway's RS485 side to
behave as an RTU SLAVE and its Ethernet side to access the LOGO! Modbus TCP
server; a gateway that only makes an RTU master is incompatible with this bus.
See [Siemens LOGO! Modbus TCP application note](https://support.industry.siemens.com/cs/attachments/109779762/109779762_LOGO_ModbusTCP_DOC_en.pdf).

## Scheduling

Round robin: PLC command read -> lux read -> PLC status write -> optional debug
write. One transaction is outstanding at a time. Defaults: 100ms between slots,
100ms response timeout, one timeout retry, 4ms quiet interval. These are
engineering defaults, configurable with baud/parity/stop bits and addresses.
No blocking wait for a reply. TX uses DMA and returns direction to RX on UART
TC (last stop bit), not DMA TC. A 20us minimum direction setup is state-driven.
TX timeout releases DE and requests asynchronous transmit abort.

Validates slave ID, function, CRC, exact byte count, exceptions, FC16 echoed
address/quantity and data ranges. RX parser is bounded to 64 bytes; reads are
limited to28 registers, writes to59. Inter-character timing conformance and
worst-case bus timing still require physical tests; this is not a certified
protocol stack. DMA chunks are accumulated, not confused with logical commands.

## PLC slave: ID1 by default

FC03, command base register4096 (`0x1000`), four holding registers:

| Offset | Value |
|---|---|
| 0 | Command sequence, increment for every new command |
| 1 | Bit0 START, bit1 STOP; STOP wins if both set |
| 2 | Mode: 0 MANUAL, 1 AUTO |
| 3 | CO2 target, 100..5000ppm in steps of10 |

First valid response after MCU boot synchronizes sequence without executing a
latched START. A new sequence is required to start. STOP is accepted on the
first response and is honored even if other fields have invalid ranges, as
long as the command word itself is in range. Same-sequence START is not replayed.
All requests enter the same burner command API used by HMI. Commissioning and
safety checks remain enforced; Modbus cannot bypass them.
PLC code should write command/mode/target first and increment sequence last.
Sequence wraps at65535 to0. Holding START does not repeatedly restart the burner.
Remote fault acknowledgement is not a v1 command; it remains local through HMI
with safety conditions checked. The PLC must monitor published inhibit/fault.
Fault value14 is ACTUATION (physical adapter / required fan synchronization).
It latches LOCKOUT during an active cycle and requires local acknowledgement
after the underlying condition is corrected. Existing fault values0..13 are unchanged.

FC16, status base4112 (`0x1010`), nine registers written by the board:
state, fault, sampling state, last command source, fault sequence high16/low16,
actuation-inhibit flag, CO2 target, original fault source. Numeric enum values
are defined in `BurnerControl.h`. No claim that Delta D-register mapping is
identical for every PLC model; adapt the PLC slave program/register map.

| Wire address | Decimal | Direction | Meaning |
|---|---:|---|---|
| 0x1000 |4096| PLC -> board | New-command sequence |
| 0x1001 |4097| PLC -> board | Bit0 START, bit1 STOP; word0..3; STOP wins |
| 0x1002 |4098| PLC -> board | Mode0 manual /1 auto |
| 0x1003 |4099| PLC -> board | CO2 target100..5000ppm, multiples of10 |
| 0x1010 |4112| board -> PLC | Burner state |
| 0x1011 |4113| board -> PLC | Fault code |
| 0x1012 |4114| board -> PLC | Sampling state |
| 0x1013 |4115| board -> PLC | Last command source |
| 0x1014 |4116| board -> PLC | Fault sequence high16 |
| 0x1015 |4117| board -> PLC | Fault sequence low16 |
| 0x1016 |4118| board -> PLC | Actuation inhibited:1=blocked,0=permitted |
| 0x1017 |4119| board -> PLC | Applied CO2 target in ppm |
| 0x1018 |4120| board -> PLC | Original fault command source |

State codes:0 IDLE,1 PRECHECK,2 PURGE,3 IGNITION_1,4 INTERPURGE,5 IGNITION_2,
6 RUNNING,7 SHUTDOWN,8 POSTPURGE,9 LOCKOUT. Source codes:0 HMI,1 PLC,2 AUTO.
Fault codes:0 NONE,1 CONFIGURATION,2 INTERLOCK,3 DETECTOR,4 FALSE_FLAME,
5 AIRFLOW,6 IGNITION_FAILED,7 FLAME_LOST,8 INPUT_STALE,9 CO_HIGH,10 CO_TIMEOUT,
11 ADJUST_LIMIT,12 COOLING_TIMEOUT,13 TEMPERATURE.
Sampling codes:0 IDLE,1 WAIT_BYPASS,2 WAIT_TEMPERATURE,3 VALVE_1,4 COOLING,
5 VALVE_2,6 WAIT_CO,7 ACCEPTED,8 AIR_SETTLE.

AUTO CO2 behavior: stop when concentration >= target; a new start is allowed
only when concentration <= target-100ppm, with fresh CO2/lux data and all other
start/safety conditions. Falling below the threshold is permission, not a bypass
of the ignition/start command state machine. MANUAL displays CO2 without using
it as a demand limit; combustion safeguards remain active.

Polling is not an emergency-stop channel. With three slots the PLC command read
is revisited after roughly300ms plus frame/quiet/processing time; timeouts/retries
lengthen this. A hardware safety stop must not depend on serial communications.

## Lux slave: ID2 by default

FC04, input register base0, two registers: unsigned lux×100, high16 then low16.
The remote node reads BH1750 and serves this value. The receiver board does not
access BH1750 over I2C. Default daylight hysteresis: ON at100lux, OFF at50lux,
invalid after4000ms without a valid response. These are configurable engineering
settings, not universal crop/greenhouse requirements. Unknown/stale lux blocks
AUTO permission; MANUAL still retains combustion safeguards.

An existing sensor with another protocol must be configured or adapted; do not
silently interpret arbitrary RS485 bytes as this register response.

## Optional debug slave: ID247

Disabled by default. PC/debug node accepts FC16 writes to register0, 48 registers:
log sequence high/low, time-ms high/low, level, then up to86 ASCII bytes packed
two per register MSB first and zero padded. Latest log is coalesced; this is not
a lossless history transport. The local DIAG ring and its overwrite count remain
authoritative. This shared-bus channel is telemetry only; local DIAG is available
without a remote debug node.

Termination, biasing, ground/reference, isolation and collision/turnaround
behavior must be verified on the physical THVD1450 link before commissioning.
