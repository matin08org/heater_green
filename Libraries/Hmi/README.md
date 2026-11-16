# Small HMI libraries for STM32F412

Implemented without LVGL, HAL dependencies in the renderers, heap allocation,
floating point, runtime PNG/font decoding, or a complete framebuffer.

## Modules

* `HmiImage`: opaque 2bpp indexed bitmaps -> native RGB565. Fixed, caller-owned
  72-byte palette context: four colors plus a 64-byte nibble lookup table.
  Four pixels are expanded per inner iteration. No assumption of aligned source
  bytes/words, no alignment casts or type-punning stores. Source cropping, row
  padding, odd widths, negative destination positions and tile clipping work.
* `HmiText`: fixed-cell 1bpp bitmap ASCII, bounded single-line strings, left /
  center / right alignment, explicit field clipping. Opaque text clears the whole
  field (including obsolete digits); transparent text preserves pixels already
  composed into the tile. Unsigned integers and signed quarter-degree values
  can be formatted without printf or floating point. Unsupported characters
  become `?`, or blank if the font does not contain `?`.
* `HmiStream`: optional compositor/transport glue. One immutable dirty-region
  snapshot at a time; at most one tile composition/start per process call.
  While one buffer belongs to the transport, the other can be rendered.
  Changes arriving during a job should update a pending application model;
  submit its newest snapshot when the current job completes. Do not mutate the
  active snapshot or palette mid-job. No ever-growing queue of stale values.
* `HmiSt7796`: optional, dedicated SPI1 adapter for this board. Existing SPI/DMA
  pins and DMA2 Stream2 stay the same. PCLK2 must be 64MHz; commands and pixels
  both use /4 = 16MHz. Reset delays, init commands, window commands and pixel
  DMA are asynchronous. 8-bit commands, 16-bit pixel frames, MINC enabled.
  RGB565 words go MSB first without a second staging buffer or byte-swap pass.
  DMA callbacks only publish flags; main polls BSY/TXE before buffer release.
  Transfer timeout is 100ms; errors latch and forbid automatic buffer reuse.
* `HmiBenchmark`: optional DWT timing for one image conversion. Does not reset
  CYCCNT (other modules already use it). Includes interrupt interference, excludes
  SPI transfer. No board timing has yet been measured.

## Asset format

I2 rows are top to bottom, pixels left to right. Four indices per byte, first
pixel in bits 7:6. Stride is in bytes and is at least `(width+3)/4`. Every row
starts on a byte boundary. Index zero is a color, not transparency.

Font cells cover consecutive ASCII codes. Each glyph is `height` rows of
`(width+7)/8` bytes, first pixel in bit 7. Padding bits are ignored. Font width,
height, advance and line height are explicit. This version uses prebuilt font
sizes, with no runtime scaling, kerning or antialiasing.

Image/text backgrounds are composed together, then sent once. To update text
over a patterned card, invalidate the union of the old and new text bounds,
decode that background region again, then draw transparent glyphs into it.
Never erase patterned backgrounds with an unrelated solid color or read LCD
GRAM. For solid fields, use the opaque mode and the corresponding theme color.
Palette changes invalidate all affected background/text pixels.

## Offline tools (Windows PowerShell / .NET)

Run from the project root:

```powershell
.\Tools\Hmi\Export-Image2.ps1 -InputPng .\card.png -OutputBase .\Debug\Hmi\card -Symbol card
.\Tools\Hmi\Export-Font.ps1 -OutputBase .\Debug\Hmi\font -FontFamily Consolas -PixelSize 18 -Symbol font
```

The image exporter produces `.i2`, `.json`, `.c` and `.preview.png`. Default
source colors map **in fixed order**: 000000, 555555, AAAAAA, FFFFFF -> 0,1,2,3.
Alpha and extra colors are rejected. `-Quantize` explicitly permits nearest-
source-color conversion, reports changed pixels, and never dithers/reorders the
palette. `-Palette` specifies four RGB565 preview colors. PNG processing is
offline; PNG is never decoded by the firmware.

The font exporter produces `.font1`, `.json`, `.c` for ASCII 32..126. Font
substitution is rejected. Check font distribution rights before shipping
generated font data. Exported cells must be visually checked at native size.

Generated C is convenient for a linked resource image. Define
`HMI_ASSET_STORAGE` as `__attribute__((section(".hmi_assets")))` when compiling
generated files, and collect that section into QSPI **before** ordinary rodata
in the final linker script. Keep the descriptors and all executable code in
internal Flash. Alternatively place raw files at known offsets and build
descriptors pointing at mapped QSPI addresses. Asset package version/CRC,
QSPI initialization and data-driven page loading belong to the asset/application
layer; these small renderers do not silently replace that layer or validate a
whole external Flash package. QSPI must be mapped/validated before using data.

## Application integration

An example card compositor lives in `examples/HmiCard.c`. It combines a
reusable indexed background and dynamic title/value from an immutable snapshot.
The same compositor works for the whole card or any dirty subregion.

```c
static uint16_t first[480 * 4], second[480 * 4]; /* 7680 bytes total */
static HmiSt7796 lcd;
static HmiStream stream;
static HmiCard snapshot;

/* After MX_GPIO_Init / MX_DMA_Init / MX_SPI1_Init, with SPI1 exclusively ours. */
HmiResult r = HmiSt7796_Init(&lcd); /* check r */
r = HmiStream_Init(&stream, HmiSt7796_Transport(&lcd),
                   480, 320, first, second, 480 * 4); /* check r */
/* Populate snapshot from already validated mapped resources and cached data. */
r = HmiStream_Submit(&stream, snapshot.screen_rectangle,
                     HmiCard_Compose, &snapshot); /* check r */

/* Each main-loop pass; keep servicing sensors/communications independently. */
r = HmiStream_Process(&stream); /* OK / BUSY / latched error */
```

All library APIs run in one main/task context. Application-owned sensor
snapshots still need their existing synchronization. Renderers do not touch
sensor SPI, actuators, keys, timers or interrupts. Never run this SPI1 driver
concurrently with `lcdPort`, `st7796.c` or `LCDPatternBench`. HAL DMA2 Stream2
IRQ handling remains the existing `HAL_DMA_IRQHandler(&hdma_spi1_tx)` path.

Build options for archives: Cortex-M4, hard-float ABI matching the project,
GCC `-O2`, function/data sections. Link with `--gc-sections` and a linker group
containing `libhmi_stream.a`, `libhmi_text.a`, `libhmi_image.a` (and the example
archive if used); HmiText needs the shared surface functions from hmi_image.
No HAL or third-party GUI source is bundled into the renderer archives.

## Resource budget and verification

For a 120x80 card: 2400 bytes of I2 data in Flash, 72 bytes for its expanded
palette, and a pair of 120x8 RGB565 buffers = 3840 bytes of RAM. For full-width
480x4 tiles the buffer pair is 7680 bytes. No input-image RAM copy or full
19200-byte card buffer is needed. Add stream/driver/application state and stack.
Offline ASCII font example at 14px generated 10x19 cells and 3610 bytes of
bitmap data; fonts are not included in the reported renderer code size.

At 16MHz, 9600 RGB565 pixels take **at least 9.6ms on the SPI wire**, excluding
window commands/gaps. CPU decode time and end-to-end board time are unmeasured.
Call `HmiBenchmark_Image2` on the real target to inspect cycles/cpu_hz. Do not
present desktop test runtimes as Cortex-M4 performance.

```powershell
.\Build-Final.ps1
.\Tests\hmi\run.ps1
.\Tests\hmi\preview.ps1
```

Host tests cover randomized reference image comparisons/guard regions,
palette changes, cropped/tiled text, shorter values, integer extremes, DMA
buffer ownership, backpressure, exact dirty regions and transport error latching.
The hardware mock checks reset delays, init/window command ordering, 16MHz
prescaler, 16-bit DMA, BSY drain, transfer timeout and failed transfer start.
The preview test runs real PNG/font exporters and the C compositor; tiled and
full output must match. Output: `Debug/Hmi/card-rendered.png`.

Current integration: the firmware uses this renderer, Home/Settings/Service/DIAG, real sensor acquisition and the process runtime. Build the application with ../../Build-Final.ps1. See ../../README.md and ../../Docs/Architecture.md. Tests are host-only and do not program hardware.
