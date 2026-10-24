# Classic Bus Compressor release notes

## v0.2.0

Classic Bus Compressor now uses realistic devices in its detector and timing network and has been
checked against measurements of a 1991 SSL Logic FX G384 hardware unit (Thompson and Heilemann,
DAFx26, dataset DOI 10.60593/ur.d.31892026, CC BY 4.0). **The sound changes.** Against that unit,
at one global calibration (0 dBFS = +22 dBu), the static gain-reduction error is now 0.28 / 0.13 /
0.04 dB at 2:1 / 4:1 / 10:1, release and Auto release times are within 0-6 %, and the gain reduction
on three music clips differs by 0.09-0.16 dB rms.

What you will hear compared with v0.1.0:

- **Softer knees.** The rectifier and clamp diodes are Shockley (1N4148 class) devices instead of
  ideal ones, so the knee comes from the exponential rather than from a hard clamp.
- **Less gain reduction at 2:1 on loud material.** The 2:1 curve flattens at high level as the
  measured unit does (14.6 dB at 0 dBFS). D7 is an assumed low-drop part, because its type is not on
  the drawing.
- **Slower release.** The timing capacitors and attack resistors are calibrated within component
  tolerance: release is about 20 % slower and Auto release about 15 % slower than in v0.1.0.
- **Second-harmonic colour under gain reduction at fast attack.** A 2.8 % imbalance between the
  rectifier's half-waves, within resistor tolerance, puts control ripple at the signal frequency, so
  H2 depends on the attack setting as measured.
- **The audio VCA control network follows the drawing again** (22.7 us pole). The sidechain control
  values stay as effective values chosen to match the measured sensitivity.
- **Lower CPU.** A block-processing path and cheaper math use about 0.2-0.5 % of real time (stereo,
  native defaults, 48 and 96 kHz, Apple M3).
- Parameters, ranges, plug-in identity and saved state are unchanged: saved sessions load, but they
  can sound different because the model is different.

Known limits: the fastest attack (0.1 ms) stays about 24 % short of the unit, and the timing values
are a calibration to one unit, not a model of every unit.

Packages:

- macOS Universal: AU and VST3, arm64 + x86_64; ad-hoc signed, not notarized.
- Windows x64: VST3, unsigned; requires the [Microsoft Visual C++ v14 x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).

Validation (Windows x64): native MSVC build on windows-2022 and CTest 1/1 passed; the packaged VST3
is a PE32+ x86-64 DLL exporting `GetPluginFactory`.

Validation (macOS, Apple Silicon): CTest 1/1 passed; both architecture slices verified;
`auval -v aufx CbCp Cdpg` passed; the VST3 loads in an offline host with the same parameters in the
same order as v0.1.0. Each ZIP
includes installation instructions, dependency notices and build provenance, with a SHA-256 checksum
companion. No DAW-host test on an end-user Windows system or Intel Mac runtime test has been
performed.

## v0.1.0

Inspired by the SSL 4000 G bus compressor: a schematic-based stereo VCA compressor following the
82E26 and 82E27 compressor drawings, with idealized device equations and plug-in extensions
(sidechain high-pass, external sidechain, parallel Mix and 0 dBFS alignment).

- macOS Universal: AU and VST3, arm64 + x86_64; ad-hoc signed, not notarized.
- Windows x64: VST3, unsigned; requires the [Microsoft Visual C++ v14 x64 Redistributable](https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist).
- Independent plug-in identity; parameter IDs and the `SslBusState` schema are preserved for
  compatible manual state transfer from the original G Bus Compressor.
- It is not a component-exact recreation of a particular unit.
