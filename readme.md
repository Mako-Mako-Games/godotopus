# Godotopus

![Godotopus](godotopus.png)

A simple and usable GDExtension addon that provides VOIP for Godot 4 using Opus encoding and decoding.

> [!WARNING]
> This is a work in progress, and our first public addon. It was originally just an internal addon for our games. Anything might end up changing, and there are likely bugs. If you encounter any issues, please report them on the [GitHub Issues page](https://github.com/Mako-Mako-Games/godotopus/issues).


## Features

* Voice activity detection
* Lightweight Opus encoding
* Packet loss compensation
* Network jitter compensation
* Config synchronization between peers
* Easy configuration
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

* **Capture Bus** - Allows you to process the microphone with Godot audio effects.
* **Direct AudioServer Input** - Lower latency and generally more reliable.

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
### Direct AudioServer Input

1. Add a `VoiceTransmitter`.
2. Leave **Capture Bus Name** empty.
3. Assign a `VoiceConfig` resource.
4. Add one or more audio players to the `Players` array.

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

---

## About

Godotopus is made by Mako Mako Games.

We're a group of recent college graduates working on our first long-term Godot game! If you'd like to follow development, check out our YouTube channel. We'll also be releasing a devlog following the development of Godotopus soon!

https://www.youtube.com/@MakoMakoDev