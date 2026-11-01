# Trigger LED behavior

The firmware uses the board's D3 LED as a visual cue that a trigger crossing was accepted. It is not a signal-present lamp, electrical trigger output, or timing measurement.

## Hardware and polarity

On the reference PCB 109-13800-00C, D3 is connected to PA15 (pin 38, JTDI) through R28 (470 Ω) to +3.3 V. The LED is active-low: PA15 LOW lights D3 and HIGH turns it off. The firmware initializes the pin only after UID-validated board activation, preloads the inactive HIGH state, and configures PA15 as a 2 MHz push-pull output while preserving the other GPIOA configuration fields. JTAG has already been released by the platform; SWD on PA13/PA14 remains available.

The `.ioc` reference records PA15 as `TRIGGER_LED`, initially HIGH. Runtime acquisition of the pin belongs to `dso_stm32_trigger_led.cpp` after the activation gate; follow the existing [build guidance](building.md) rather than regenerating startup code from the `.ioc`.

## When it lights

Each newly accepted trigger crossing starts or restarts a 100 ms pulse serviced in the foreground without waiting for display refresh. Each acquisition engine latches a pending event until the application consumes it. These flags survive rearming, are consumed together even during a fault, and coalesce multiple crossings between service calls into one indication. Replayed frames or switching acquisition modes do not invent an event.

- Eligible actual trigger edges in Auto, Normal, or Single can light D3.
- Accepted Normal or Single edges on slow timebases can light D3 before the processed post-trigger record is ready.
- Single's pulse is allowed to finish while the instrument enters automatic HOLD.
- Auto free-run timeouts, recall/HOLD redraws and slow Auto rolling updates do not produce pulses. Crossings rejected before sufficient pre-trigger history is available do not count.
- Closely spaced accepted edges restart the pulse, so D3 may look continuously lit.

D3 is initialized off and is driven inactive on application faults; the HardFault/NMI entry path also makes PA15 inactive. The new software behavior has not been flashed or tested on hardware. Use the displayed waveform and acquisition state for instrument operation, not LED timing.
