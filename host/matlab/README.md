# MATLAB USB client

`DsoUsbClient.m` reads captures from a USB CDC COM port using the DSO waveform protocol v1. The verified bench use is `getHeldFrame()` on a standalone held capture. The scope must be powered from its normal 9 V input; the USB connection carries data and joins the instrument ground to the PC ground.

The ordinary firmware profile keeps USB disabled. This client requires an explicitly configured `DSO_USB_STOCK_BENCH_TRIAL` image on the reference board. That trial uses the stock fixed pull-up; it is an experiment, not an electrically compliant production configuration. The firmware source defaults its VID/PID to zero. Do not redistribute a private bench image built with test identifiers.

## Read the held frame

In MATLAB R2025b, use the COM port shown by Windows Device Manager. The verified reference setup appeared as `COM7`; replace it with the port assigned on your PC.

```matlab
client = DsoUsbClient("COM7");
cleanup = onCleanup(@() delete(client));
frame = client.getHeldFrame();
plot(frame.TimeSeconds, double(frame.Raw));
xlabel("Time from trigger (s)");
ylabel("ADC count");
grid on;
```

`getHeldFrame()` sends INFO and read-only frame lease/sample commands, then releases the lease. It does not change settings or write Flash. `frame.Raw` contains 12-bit ADC codes; `frame.TimeSeconds` is calculated from the capture timer metadata. `frame.VoltageVolts` is empty when voltage conversion is unavailable. Otherwise the client converts with the metadata supplied by firmware. The verified smoke capture reported nominal calibration kind 1, not a measured calibration profile.

The returned metadata includes sample count, pretrigger count, timer frequency and divider, coupling, trigger mode/edge, and calibration fields. Raw codes are the authoritative data. A held export requires a complete frame to be available on the scope.

## Capture methods

The client exposes `capture(timeoutMs)` and `captureRepeated(count, timeoutMs, gapSeconds)`. They request separate Single captures; they do not promise continuous or gap-free acquisition. Before capture, the scope must be in HOLD and foreground work must be idle. A request while RUN is active may be rejected as busy; the host does not take over the scope or switch it to HOLD. The R6 `captureRepeated(3,uint32(3000),0)` check returned three 1,024-sample records in 3.361 seconds with distinct increasing epochs and monotonic time axes. Each capture has acquisition/presentation dead time, even when the requested extra gap is zero.

`delete(client)` closes the COM connection and releases any outstanding frame lease or host capture job. The `onCleanup` pattern above also performs cleanup if the script exits with an error. USB CDC ignores the nominal 115200 baud setting and the client does not toggle DTR.

## Current hardware check

The current R6 public client completed live INFO and held-frame export, then exported the same frozen frame after Spectrum and return to Scope. RAW samples and frozen metadata matched across those domain checks. The independent FFT oracle matched peak bin 1, with a -0.0022 dB observed-to-predicted level difference for that record. A separate host Single capture also completed and was exported. The Python host check completed three Single captures; acknowledgments took 1.07–1.17 ms, completion 79.14–80.20 ms, and RAW reads 47.89–50.74 ms (40.37–42.76 kB/s of sample bytes). Cancellation returned `Cancelled`; a 1 ms deadline returned `Timeout` and left no pending job.

One debug-attached domain-presentation observation took 64.82 million DWT cycles / 1.75 seconds including probe work for Spectrum and 64.96 million cycles / 1.78 seconds on return. These are bounded end-to-end observations, not FFT-only timings. Firmware state and screen counters advanced, but no independent visual confirmation of scope contents or analog metrology was performed. Calibration metadata was nominal, not measured. Historical R4/R5 repeat checks also passed; R5 completed in 2.524 seconds. Electrical production compliance and fast repeated acquisition remain unqualified.
