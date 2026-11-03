# Troubleshooting

## Blank display on first build

A blank screen is expected while the public source still contains zero UID words and a zero board identity. Startup stops at the identity gate before initializing the display. Configure your own UID and matching identity as described in [Getting started](getting-started.md#3-set-your-own-board-identity). Do not bypass the identity or hardware guards.

## Blank display after identity configuration

Confirm the board is PCB 109-13800-00C with an ILI9341 display and matching STM32F103 device/64 KiB Flash. Check normal instrument power and shared SWD ground; the programmer's 3.3 V reference alone does not power the analog front end. If programming verification fails, do not power up the instrument or erase the chip as a first response; preserve the storage page and diagnose the SWD/readback issue.

## Build errors

Import `firmware/` as an existing STM32CubeIDE project and select **Release**. Use the documented toolchain and build the current project as supplied. Do not regenerate the `.ioc` as a first troubleshooting step; generated C startup and IRQ code requires deliberate integration with the C++ platform files.

## Unexpected waveform

Check probe and ground connections, input coupling and sensitivity switches, trigger mode and trigger level. Auto triggering is useful for changing signals; Normal triggering is useful for repetitive signals; Single waits for one qualifying trigger. Confirm the signal is within the instrument's safe input range. The current firmware supports 18 triggered timebases from 10 µs/div through 5 s/div.

## Trigger LED stays off or appears continuously lit

D3 is an activity cue for accepted trigger crossings. It stays off when there is no newly accepted edge, during Auto free-run timeout, held-waveform redraw, or a startup/application fault. Closely spaced crossings can keep it apparently lit without a visible gap. The operator confirmed it working on the reference instrument, but its pulse timing is not instrument-measured. See [Trigger LED behavior](trigger-led.md).

## Unexpected readings or saved records

Voltage readings depend on the individual analog front end. Default gain and zero settings are not a laboratory calibration and may not transfer between instruments. The final 1 KiB of Flash stores scope settings and DC calibration. Waveform save/recall is not available. Avoid erasing the storage page if you need to retain those settings and calibration.

The unreleased single-bank storage format can lose previously saved data if reset or power fails during a save. The old lower bank is application space; previous saved records are not guaranteed to migrate.
