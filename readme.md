# Godotopus

![Godotopus](godotopus.png)

A simple and usable GDExtension addon that provides VOIP for Godot 4 using Opus encoding and decoding.

> [!WARNING]
> This is a work in progress, and our first public addon. It was originally just an internal addon for our games. Anything might end up changing, and there are likely bugs. If you encounter any issues, please report them on the [GitHub Issues page](https://github.com/Mako-Mako-Games/godotopus/issues).


## Features

* Voice activity detection via Opus's own RNN-based DTX — no hand-tuned
  amplitude gate, and near-zero bandwidth while you're not talking
* Lightweight Opus encoding, with in-band FEC for single-frame packet loss
* Deep Redundancy (DRED) for recovering much longer packet loss bursts than
  FEC alone can, plus libopus 1.5+'s DNN-based packet loss concealment and
  1.6+'s blind bandwidth extension when built with `scons dred=yes` (see
  [Building](#building))
* Network jitter compensation with a hard safety valve — a stall (yours or a
  peer's) resyncs to the latest audio instead of ever building up a growing
  latency backlog
* Keeps working across local `SceneTree` pauses (debug menus, pause menus,
  etc.) instead of freezing and then dumping a latency spike on resume
* Config synchronization between peers
* Easy configuration, with an optional latency/diagnostics logging mode
  (`VoiceConfig.debug_log_latency`) to help track down buffering issues
* Output to multiple audio players
* Supports all `AudioStreamPlayer` node types

---

## Installation

1. Download the latest release for your platform.
2. Extract it into your project's `addons/` folder.
3. Enable the plugin in **Project Settings → Plugins**.

---

## Usage

Godotopus supports two methods of capturing microphone input:

* **Direct AudioServer Input** (default, recommended) - Pulls mic input
  straight from the `AudioServer`, bypassing Godot's audio bus/effect graph
  entirely.
* **Capture Bus** - Routes the microphone through a Godot audio bus first, so
  you can apply bus effects (e.g. a noise gate) to it before encoding.

> [!WARNING]
> The Capture Bus path has a confirmed Godot engine issue where its internal
> buffering can build up several seconds of latency across engine
> hitches/`SceneTree` pauses, in a way Direct AudioServer Input does not
> exhibit at all. This isn't something Godotopus can work around at the
> script level — it's the entire reason Direct AudioServer Input exists.
> Only use Capture Bus if you specifically need bus effects on the mic
> signal before it's encoded, and accept that latency tradeoff.

### Direct AudioServer Input

1. Add a `VoiceTransmitter`.
2. Leave **Capture Bus Name** empty (the default).
3. Assign a `VoiceConfig` resource.
4. Add one or more audio players to the `Players` array.

### Capture Bus

1. Create a new audio bus (for example, `VoiceCapture`).
2. Add an `AudioEffectCapture` effect to that bus.
3. Add an `AudioStreamPlayer` somewhere in your main scene and assign it an `AudioStreamMicrophone`.
4. Add a `VoiceTransmitter`.
5. Set **Capture Bus Name** to the name of the bus you created.
6. Assign a `VoiceConfig` resource (the defaults are fine).
7. Add one or more audio players to the `Players` array.

> **Note**
>
> Make _SURE_ you don't end up creating multiple microphone recording AudioStreamPlayer nodes. I spent 5 hours straight trying to figure out why people's voices were playing back twice just because I added it under a scene that gets instantiated multiple times!

---

## Building

Clone the repository with submodules:

```bash
git clone --recursive https://github.com/Mako-Mako-Games/godotopus.git
```

or

```bash
git clone --recursive git@github.com:Mako-Mako-Games/godotopus.git
```

Build from the repository root:

```bash
scons
```

Copy `build/addons/godotopus/` into your project's `addons/` folder.

> You may need to build `third-party/godot-cpp` first:
>
> ```bash
> cd third-party/godot-cpp
> scons
> ```

### Optional: DRED / DNN-based PLC / OSCE bandwidth extension

libopus's deep-learning features (Deep REDundancy, DNN-based packet loss
concealment, and OSCE speech enhancement + blind bandwidth extension) need a
~130MB set of pretrained-weight source files that aren't checked into the
`opus` submodule by default. They roughly double compile time and binary
size, so they're opt-in:

```bash
scons dred=yes
```

The weights are downloaded automatically (and cached) the first time you
build with this flag on. Without it, `VoiceConfig.dred_duration_ms` and
`enable_bandwidth_extension` are harmless no-ops — everything else (DTX,
in-band FEC, jitter buffer) works the same either way.

---

## About

Godotopus is made by Mako Mako Games.

* Berti - Programmer
* Pockette - UI/UX
* Sazarn - Artist


We're a group of recent college graduates working on our first long-term Godot game. If you'd like to follow development, check out our YouTube channel.

[![YouTube Logo](youtube-logo.png)](https://www.youtube.com/@MakoMakoDev)

https://www.youtube.com/@MakoMakoDev
