# Classic Bus Compressor

Inspired by the SSL 4000 G bus compressor.

Classic Bus Compressor is a stereo VCA bus compressor plug-in built with JUCE 8 for AU and VST3.
Its DSP follows the compressor sections of the 82E26 and 82E27 circuit drawings, with realistic
diodes, fitted timing and plug-in extensions. It was checked against measurements of one hardware
unit, but it is not a component-exact recreation of every unit.

![Classic Bus Compressor interface](docs/editor.png)

This release has its own plug-in identity (`com.c0dypeng.classicbuscompressor`, `Cdpg/CbCp`) so it
can coexist with the original G Bus Compressor plug-in. Parameter IDs, order, ranges and defaults,
along with the `SslBusState` saved-state root, are unchanged from v0.1.0 and remain stable for
existing sessions and presets. Existing DAW instances keep the identity they were made with and are
not replaced automatically.

## Downloads

Get the latest builds from [GitHub Releases](https://github.com/chijenpeng/classic-bus-compressor/releases/latest).
The macOS package provides universal AU and VST3 plug-ins for Apple Silicon and Intel Macs; it is
ad-hoc signed and not notarized. The Windows package provides an unsigned x64 VST3 plug-in.

The control detents follow the
[SSL XLogic G-Series owner's manual](https://support.download.solidstatelogic.com/G-Series%20Bus%20Comp/G-Series%20Bus%20Compressor%20User%20Guide.pdf).

## Controls

| Control | Range | Meaning |
| --- | --- | --- |
| Threshold | −100 to +100% | Position of the original 50k linear threshold pot. Lower settings compress more. |
| Ratio | 2:1, 4:1, 10:1 | Switches the original resistor and diode ratio network. |
| Attack | 0.1, 0.3, 1, 3, 10, 30 ms | Selects the original R1–R6 resistors. |
| Release | 0.1, 0.3, 0.6, 1.2 s, Auto | Selects the original RC branches; Auto solves both coupled capacitor nodes. |
| Make Up | 0–100% | Position of the original 25k linear make-up pot fed from +12 V through 4.7k. |
| S/C HPF | Off, 30, 60, 105, 125, 185 Hz | Filters detector only. This is a modern SSL extension. |
| External S/C | Off/On | Uses host sidechain input when connected. |
| Mix | 0–100% | Dry/wet parallel compression; 100% is classic. |
| 0 dBFS alignment | +12 to +30 dBu | Sets the analogue peak voltage represented by 0 dBFS. Default +22 dBu. |
| Compressor In | Off/On | Off is a bit-exact dry path. |

## Circuit mapping and limits

The [SSL 4000G service manual](https://funkwerkes.com/web/wp-content/techdocs/MixedProAudio/SSL-4000G-Console-Service-Manual-v1_1.pdf)
contains the original 82E26 and 82E27 schematics (PDF pages 203 and 206). The DSP implements:

| Schematic | Implemented equation |
| --- | --- |
| 82E26 C3/C4 with R7, C9 with R12, C11/C12 with R19, C18 with R34 | Three cascaded audio coupling poles near 1.06, 3.48 and 1.06 Hz; a detector coupling pole near 1.17 Hz. The internal detector taps the signal after the first audio coupling stage. |
| 82E26 dbx 202X, R11/R32 | Exponential VCA with the [202X data sheet](https://audiocircuit.dk/downloads/dbx/DBX-202-vca-sm.pdf) −20 dB/V control law and 1.25k internal input resistance. The sidechain's zero-control voltage gain is 33k/(91k+1.25k). R30 is a J112 switch, not a 47k resistor. The audio VCA uses R12=3.32k and R16=4.53k. |
| 82E27 T2, RV3 + R72, C12 (audio VCA control) | The drawn network: T2 drives the two audio 202X control ports (832 Ω each, data sheet, two 82E26 cards in parallel = 416 Ω) through RV3 (1k) + R72 (47R), with C12 100 nF to ground; R51 is not fitted. RV3 is trimmed by measurement ("SET 5.2 VOLTS ON VCA"); it is set to 500 Ω in total (RV3 about 453 Ω), which keeps the static sensitivity of the earlier model and gives the drawn control pole, 100 nF × (500 Ω ‖ 416 Ω) = 22.7 µs. |
| 82E27 T3, R52, C11 (sidechain VCA control) | Kept as effective values, not the drawn ones: 100R into a single 83.2 Ω load (4.5 µs). Those values matched the measured unit's sensitivity; the drawn network (100R into two 832 Ω ports) would double it. **Open question**: the drawn values would roughly double the sidechain sensitivity, which the measured static curves do not show; the sidechain control path is not yet explained. |
| 82E26 T6/T7, R36–R45 | Full-wave rectifier and the resistor equations for all three ratio contacts. The two half-waves are unbalanced by 2.8 % (R36 about 1.4 % off in the R34–R37 balance, inside 1 % resistor tolerance), which puts control-voltage ripple at the signal frequency: the second harmonic under gain reduction then depends on the attack setting, as measured. |
| 82E26 D4, D6 and 82E27 attack resistors | D4 (T7 output) and D6 (into the timing node) are Shockley diodes of the 1N4148 class (Is 2.5 nA, n 1.84, Rs 0.6 Ω). Their drops cancel only when their currents match, so the knees come from the exponential rather than from an ideal clamp. D6 and the attack resistor charge the timing capacitor as a Shockley branch, solved in closed form with the Wright omega function (interpolated Hermite table, built in the constructor, error below −130 dB). |
| 82E26 D7 (2:1 network) | A Shockley diode as well, but its type is **not on the drawing**. It is assumed to be a low-drop part (Is 1 µA, n 1.3, germanium or Schottky class): that reproduces the measured flattening of the 2:1 curve at high level, which a 1N4148 gives only half of. It is an assumption, to be settled by a parts list or a second unit. |
| Two 82E26 cards | Independent left/right detector paths with a common diode-OR timing feed and stereo VCA control. |
| 82E27 R1–R6, R9–R12, C3–C6 | Switched attack resistance and release RC branches, integrated each sample. Calibrated to the measured G384 within component tolerance: C3–C6 ×1.2 (electrolytic, ±20 %), attack resistors ×1.05 (±5 %). |
| 82E27 C1/R7 and C2/R8 | Coupled two-node Auto release network, solved as a 2×2 nodal system; C1 and C2 ×1.2 (±20 %). |
| 82E27 T2/T3, R19–R22, R25/R27 | Separate audio make-up and sidechain threshold control-voltage sums. R21 connects to the timing node, R27 is T3 feedback; the lines cross without a junction. The threshold pot voltage follows the 50k linear element and its ±12V, 3.9k end resistors. |

The threshold control spans the drawn potentiometer's full travel. Its
percentage is physical wiper position, not a dBu mark. The default +50% is
a usable starting position, not a documented factory setting. The audio VCA
control resistor is set within the R72/RV3 trim range to keep the earlier
sensitivity; the drawing specifies adjustment by measurement, so its exact setting
still needs a reference unit. The make-up pot spans approximately 0–10.1 V at its
wiper, or roughly 0–14.7 dB of VCA gain with that audio control trim.
D2, D3 and D5 stay ideal; dbx 202X distortion, noise, fader circuitry and
rail-dependent transient behavior are not modelled. HPF, external sidechain,
parallel Mix and DAW alignment are plug-in extensions.

### Checked against a measured unit

The model was compared with measurements of a 1991 SSL Logic FX G384 (Thompson and Heilemann,
DAFx26, dataset DOI 10.60593/ur.d.31892026, CC BY 4.0) at one global calibration: 0 dBFS = +22 dBu
and the threshold wiper at 3.65 V, with no per-setting fitting. Against the 90 ratio/attack/release
permutations, three music clips and a 1 s burst harmonic analysis:

| Measure | Result |
| --- | --- |
| Static gain-reduction curve, rms error 2:1 / 4:1 / 10:1 | 0.28 / 0.13 / 0.04 dB |
| Gain reduction at 0 dBFS, 2:1 / 4:1 / 10:1 | 14.6 / 16.4 / 16.7 dB (unit 14.5 / 16.3 / 16.7) |
| Knee width, 2:1 / 4:1 | 3.9 / 0.6 dB (unit 3.9 / 0.8) |
| Release t63, ratio to unit, 0.1 / 0.3 / 0.6 / 1.2 s | 0.99 / 1.05 / 1.01 / 1.00 |
| Auto release t63, ratio to unit, 10 ms / 100 ms / 1 s bursts | 1.06 / 1.02 / 1.05 |
| Attack t63, ratio to unit, 0.1 / 1 / 30 ms | 0.76 / 0.92 / 1.00 |
| Music, gain-reduction rms difference (three settings) | 0.09 / 0.15 / 0.16 dB, correlation above 0.998 |
| H2 and H3 under about 10 dB of reduction, rms error over 90 permutations | 1.7 / 0.7 dB |

The fastest attacks stay 24 % short of the unit at 0.1 ms: the unit's gain reduction starts about
0.1 ms after the burst, which the 22.7 µs control pole explains only in part. The timing values
above are a calibration to one unit, not a model of every unit.

CPU: with the block-processing path, stereo at native defaults, 48 kHz and 96 kHz on an Apple M3
(arm64 build) the compressor used about 0.2 to 0.5 % of real time. Actual use depends on the
computer, sample rate and settings.

## Build

Requires CMake 3.22+, Ninja, and Xcode command-line tools on macOS. The project uses JUCE 8.0.9,
from `third_party/JUCE`, `JUCE_SOURCE_DIR`, or the pinned CMake FetchContent fallback.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCOPY_PLUGIN_AFTER_BUILD=OFF
cmake --build build --target ClassicBusCompressor_AU ClassicBusCompressor_VST3 classic_bus_test classic_bus_snapshot -j2
ctest --test-dir build --output-on-failure
./build/classic_bus_snapshot_artefacts/Release/classic_bus_snapshot docs/editor.png
```

For the universal macOS package, add `-DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"` and run
`tools/package_macos.sh` from a clean, committed tree; it writes
`build-release/dist/classic-bus-compressor-<version>-macos-universal.zip` and a `.sha256` companion.

Plug-in copying is disabled by default. To copy AU and VST3 bundles into the current user's audio
plug-in folders, explicitly configure with `-DCOPY_PLUGIN_AFTER_BUILD=ON`.

## Validation

Version 0.2.0 on macOS (Apple Silicon): CTest 1/1 (`classic_bus_test`: transfer curves, timing,
harmonics, stereo link, the Wright omega table's error bound and the diode solver), and universal
arm64 + x86_64 AU and VST3 slices, `auval -v aufx CbCp Cdpg` passed, and the VST3 loads in an
offline host with the same parameters in the same order as v0.1.0. The measured-unit comparison above is the model's accuracy check;
the measurements themselves are not part of this repository. The Windows package is built and tested
by the repository's release workflow. No DAW-host test on an end-user Windows system or an Intel Mac
runtime test has been performed.

See [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for JUCE, font and SDK notices.
