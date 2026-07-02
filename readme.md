# Godotocpus
![Godotopus](godotopus.png)
A simple GDExtension library for encoding and decoding Opus audio in Godot 4.

## How to Build

1. Clone the repository with recursive submodules:

```bash
	git clone --recursive gitrepo
```

2. Build using SCons (run in the root of the repository):

```bash
	scons
```

3. Copy the generated `addons/godotopus/` folder into your Godot project's `addons/` folder.
4. ???
5. Use the codec!

> Note: You might have to cd to the `third-party/godot-cpp/` folder and run scons first. I haven't checked yet!

## How to Use

```gdscript
var sample_rate := 48000
var channels := 1
var frame_size := 960  # 20ms at 48000hz

# Create and initialize
var encoder := GodotOpusEncoder.new()
var decoder := GodotOpusDecoder.new()
encoder.initialize(sample_rate, channels)
decoder.initialize(sample_rate, channels)

# Encode
var packet: PackedByteArray = encoder.encode(pcm)

# Decode
var decoded: PackedFloat32Array = decoder.decode(packet)
```
