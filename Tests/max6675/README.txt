MAX6675 / 74HC595 shared-SPI implementation
=========================================

Integration and access
----------------------
SPI2 is Mode 0, 8-bit, MSB first, PCLK1/32 = 1 MHz at the current 64 MHz
system clock / 32 MHz APB1 clock. spi.c and the CubeMX .ioc both agree.
The display uses SPI1. SPI2 raw HAL transfers now exist only in HC595.c.
SPI2 uses RX DMA1 Stream3 and TX DMA1 Stream4, channel 0. Existing DMA/SPI IRQ handlers call HAL. RX priority is VERY_HIGH and TX is HIGH.

App_Init schedules the startup Q0=1 frame through HC595_Init/MAX6675_Init.
App_Process services MAX6675_Process once per loop. main does NOT initiate
sensor transactions; it reads the local `temperatures` snapshot using:

    MAX6675_Snapshot_t temperatures;
    MAX6675_GetSnapshot(&temperatures);
    if (temperatures.sensor[0].valid) {
        float c = temperatures.sensor[0].temperature_q4 * 0.25f;
        /* consume fresh, healthy sensor 1 reading */
    }

sensor[0] = MAX6675_SENSOR_1; sensor[1] = MAX6675_SENSOR_2.
The getter copies both cached records and capture time under PRIMASK, restoring
the caller's previous interrupt mask. It does no SPI and waits for no hardware.
Service functions run in ONE main/task context. IRQ callbacks only publish
completion/error flags, with a memory barrier for the RX buffer. Public 595
setters and snapshots use short IRQ critical sections, not volatile alone.

Each record includes last valid temperature_q4 / temperature_c, has_valid_sample,
last_status, last_valid_ms, last_attempt_ms, spi_error_count, open_error_count,
raw from the latest completed SPI read, and stale/valid computed at snapshot time.
Errors update status/attempt time/counters without overwriting the last good
value or its timestamp. stale means no valid sample yet, or age > 2000 ms.
valid means has_valid_sample AND NOT stale AND last_status == MAX6675_OK.
An old good value can still be inspected with has_valid_sample even on error;
it must not be presented as a new healthy sample. GetTemperature only assigns
its float output for a fresh healthy sample. GetData returns the last attempt
status, so its stale/valid fields must also be checked.
Before the first good sample the UI shows "---", not a fictitious zero reading.
A genuine valid zero-degree reading is accepted and displayed normally.
The legacy temperature_1/temperature_2 globals are main-only display mirrors;
use GetSnapshot for a coherent interface and metadata.

Timeline and ownership
----------------------
The installed chain contains one 8-bit 595 (HC595_OUTPUT_COUNT=8). Every latch
is preceded by a NEW full 8-bit frame from the shared shadow. If the hardware
chain grows, this frame and shadow must be extended to cover the whole chain.

1. Initial full frame Q0=1 is sent with TransmitReceive_DMA, then latched.
   Neither sensor has a valid sample. Sensor 1 begins conversion.
2. Wait at least 500 ms measured AFTER that physical latch. Full frame Q0=0,
   latch, wait at least MAX6675_CS_SETTLE_US=20 us, read exactly two SPI bytes
   from sensor 1. Sensor 2 conversion started at this same latch.
3. At least 500 ms after that latch, full frame Q0=1, latch, settle, read sensor 2.
4. Repeat, with each sensor nominally updated about once per second.

The conversion comparison waits one extra SysTick tick to avoid a sub-ms early
selection caused by tick quantization. Timing and age arithmetic handle 32-bit
tick wrap. Settling uses DWT CYCCNT at SystemCoreClock, with no busy wait.
Both conversion and settling constants are overrideable in MAX6675.h.
Actual intervals can be longer if the main loop is busy with other legacy work
(e.g. UART/display); these delays cannot shorten conversion or settle minima.

The 595 bus state owns SPI2 from the selection transfer through latch, settle,
16-bit read and result consumption, including timeout/error recovery. Public
595 writes only queue shadow changes during this interval. They cannot send
clocks or pulse STCP. Q0 pin/toggle requests are rejected; byte/SetAll/ClearAll
requests preserve Q0. Only MAX6675 uses the private HC595_SensorBus interface.
Public HC595 write HAL_OK now means queued, NOT physically latched yet.
No direct writes to STCP, SPI2 or Q0 should be added elsewhere.
Normal output writes during conversion preserve Q0 and therefore do not
restart conversion. Their shared-clock data from the currently selected
sensor is discarded. A failed select never latches partial data, and Q0 shadow
is rolled back so a later public output update cannot switch CS unexpectedly.

Raw = (rx[0] << 8) | rx[1]. D2 marks open thermocouple; D0 is ignored.
Valid quarter-degrees = (raw >> 3) & 0x0FFF. SPI failure and open thermocouple
are separate statuses/counters. A 10 ms transfer watchdog recovers a lost IRQ
without a blocking HAL abort loop or latching a partial serial register.
If startup communication fails, App_Process retries initialization without
claiming any valid sample. A runtime failed select is retried after a delay;
a failed read after a successful latch still advances to the other sensor's
conversion deadline. Other channels continue after read errors.

Validation completed
--------------------
Tests/max6675/run.ps1 compiles the actual HC595.c and MAX6675.c with a mock HAL
and runs normal, tick-rollover and startup-failure scenarios. All passed.
The tests enforce exclusive transactions, 500 ms conversion minimum, 20 us
settle minimum, two-byte reads, no latching of RX leftovers, deferred public
updates, Q0 preservation, D0 ignore, D2 detection, zero-degree readings,
last-good retention, separate error counters, SPI read/select failure, lost
completion IRQ recovery, stale data and getters that issue no SPI.
These tests simulate signals; they do not measure electrical timing on a board.
Full ARM Debug compile/link and BIN/HEX generation passed. The CLI verification
build used `make C_DEPS= all` because existing generated dependency files contain
absolute drive-letter targets that this make invocation cannot parse. This
bypasses old dependency includes; the changed C files were rebuilt and linked.

Electrical review and pending bench validation
---------------------------------------------
The user confirmed both MAX6675s and the 74HC595 use 3.3 V. MAX6675 is specified
for 3.0-5.5 V and up to 4.3 MHz; 1 MHz is below that limit. HC595 can operate
at 3.3 V. Shared 3.3 V logic is consistent with the datasheet supply/input
limits, assuming normal loading, common ground, and a 3.3 V inverter pull-up.
Supply integrity, slew, loading and the transistor timing were NOT measured.

No logic analyzer/oscilloscope capture or physical thermocouple disconnect /
reconnect test was available. This firmware has NOT been flashed in this task.
Required checks after the user programs the updated Debug firmware:

* Capture PB10/SCK, PC2/MISO, PA0/STCP and BOTH actual sensor CS pins.
* Verify each CS HIGH conversion interval is >= 500 ms, including initialization.
* Trigger on STCP rising; the newly selected sensor's CS and MISO must settle
  before the first of exactly 16 read clocks. Start with 20 us; increase this
  compile-time value if the measured transistor/signal settling needs it.
* STCP must remain LOW during the read; other 595 outputs must stay unchanged.
  Before each subsequent latch, confirm a complete new 8-bit image is sent.
* Check the complementary CS transitions for any brief both-selected interval.
  Waiting before reading does not eliminate contention during the transition;
  address problematic overlap electrically if observed.
* Disconnect/reconnect each thermocouple: observe last_status/open_error_count,
  unchanged last-good temperature/timestamp on error, then renewed valid data.
* Inject an SPI fault/lost service: observe spi_error_count and preserved data.
  After > 2 s without a good update, stale must become true even with old data.

Primary specifications reviewed:
https://www.analog.com/media/en/technical-documentation/data-sheets/MAX6675.pdf
https://www.ti.com/lit/ds/symlink/sn74hc595.pdf

On-board SPI overrun investigation and DMA update
------------------------------------------------
Live debug of the previously programmed firmware caught HAL_SPI_ErrorCallback
with hspi2.ErrorCode=4 (HAL_SPI_ERROR_OVR), SENSOR_RX state, TxXferCount=0 and
RxXferCount=2. Both sensors had repeated SPI_ERROR attempts and no valid sample.
SPI2 NVIC was enabled and mode/prescaler matched Mode 0, 1 MHz. This establishes
receive overrun, rather than thermocouple-open D2, as the observed error.
CPU executes code through external QSPI; per-byte interrupt service latency is
a plausible contributor, but its precise timing was not measured on a scope.

HC595.c now starts both the full-shadow write and the 16-bit read using
HAL_SPI_TransmitReceive_DMA. Buffers are static and remain alive throughout the
transfer; RX DMA drains DR without per-byte CPU service. Completion/error IRQs
still publish flags to the same main-context state machine. The conversion,
settle, latch/bus ownership and snapshot semantics remain unchanged.

Recovery first disables SPI DMA requests and both streams. RECOVERING retains
bus ownership until stream EN bits clear, then clears DMA flags and restores
HAL states. It never busy-waits for abort or releases buffers while DMA is live.
The three mocked-C test scenarios and ARM build/link passed after this change.
The DMA firmware has NOT been programmed/validated on the physical board yet.
Program the newly built Debug/Main_Board_Cube.bin, then check that valid samples
arrive and error counters no longer rise; physical waveform checks still apply.
