# Godotopus

![Godotopus](godotopus.png)
A simple but powerful GDExtension addon enabling VOIP using Opus encoding and decoding for Godot 4.

## Features

- Voice activity detection
- Lightweight Opus encoding
- Packet loss compensation
- Easy configuration
- Config synchronization between peers
- Network jitter compensation
- Output to multiple AudioPlayer nodes
- Support for all AudioPlayer nodes

## How to install

1. Go to the releases page and download the built addon for your platform.
2. Extract the contents to the `addons/` folder of your project

## How to Use

There are two ways to capture input audio using Godotopus.

- Capture Bus - Allows using effects directly on the voice signal
- AudioServer - Lower latency, more reliable

### Using Capture Bus

In order to record using Godot's `AudioEffectCapture`:

First, create an audio bus. Name it whatever you wish, "VoiceCapture" for example.

Add an AudioEffectCapture effect to the audio bus.

Next, add an AudioStreamPlayer to your main game scene, and place an `AudioStreamMicrophone` as the stream.

> NOTE: Make _SURE_ you don't end up creating multiple microphone recording AudioStreamPlayer nodes. I spent 5 hours straight trying to figure out why people's voices were playing back twice just because I added it under the player scene!

Now, add a VoiceTransmitter under your player (or where ever is most convenient to you).

Set the Capture Bus Name field on the `VoiceTransmitter` to the name of the bus you set up.

Add a `VoiceConfig` to the `VoiceTransmitter` (default settings are fine)

Finally, add an Audio Stream player, and register it to the `VoiceTransmitter`'s Players array.

Done!

### Using Direct Input

In order to record using Godot's `AudioServer`:

First, add a VoiceTransmitter under your player (or where ever is most convenient to you).

Next, set the Capture Bus Name field to empty to trigger usage of the AudioServer

Add a `VoiceConfig` to the `VoiceTransmitter` (default settings are fine)

Finally, add an Audio Stream player, and register it to the `VoiceTransmitter`'s Players array.

Done!

## How to Build

1. Clone the repository with recursive submodules:

```bash
git clone --recursive https://github.com/Mako-Mako-Games/godotopus.git
```

or

```bash
git clone --recursive git@github.com:Mako-Mako-Games/godotopus.git
```

2. Build using SCons (run in the root of the repository):

```bash
scons
```

3. Copy the generated `build/addons/godotopus/` folder into your Godot project's `addons/` folder.
4. ???

> Note: You might have to cd to the `third-party/godot-cpp/` folder and run scons first to build godot-cpp. I haven't checked yet!

## Godotopus was made by Mako Mako Games!

We're a group of recent college graduates working on our first longterm Godot Game project! Consider checking on us from time to time!
<br>

[![YouTube Logo](youtube-logo.png)](https://www.youtube.com/@MakoMakoDev)
[Our YouTube Channel](https://www.youtube.com/@MakoMakoDev)
