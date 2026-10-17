@tool
class_name VoiceEncoderSettings
extends Resource
## How a [VoiceSender] encodes the microphone.
##
## A preset: assigning it to a sender gives the sender its own copy, so one
## [code].tres[/code] can be shared by many senders. To change a running
## sender, edit [member VoiceSender.settings] (its copy). Codec options apply
## immediately; [member sample_rate], [member channels] and
## [member frame_duration_ms] restart the encoder. Receivers don't need to
## match any of this: every Opus packet describes its own format.

const SAMPLE_RATES: Array[int] = [8000, 12000, 16000, 24000, 48000]
const FRAME_DURATIONS: Array[int] = [5, 10, 20, 40, 60]

## Rate the microphone is resampled to before encoding. Opus picks its coded
## bandwidth from [member bitrate], so lower rates mainly save CPU.
@export_enum("8000:8000", "12000:12000", "16000:16000", "24000:24000", "48000:48000")
var sample_rate: int = 48000:
	set(value):
		value = _snap(value, SAMPLE_RATES)
		if sample_rate != value:
			sample_rate = value
			emit_changed()

## 1 for mono, 2 for stereo. Voice chat is almost always mono.
@export_enum("Mono:1", "Stereo:2") var channels: int = 1:
	set(value):
		value = 2 if value == 2 else 1
		if channels != value:
			channels = value
			emit_changed()

## Audio per packet. Shorter frames lower latency; longer frames save
## per-packet overhead.
@export_enum("5:5", "10:10", "20:20", "40:40", "60:60") var frame_duration_ms: int = 20:
	set(value):
		value = _snap(value, FRAME_DURATIONS)
		if frame_duration_ms != value:
			frame_duration_ms = value
			emit_changed()

## Target bitrate in bits per second, the main quality/bandwidth control.
## Around 16000 is wideband speech; 24000 and up is near transparent.
@export_range(6000, 128000, 1000, "suffix:b/s") var bitrate: int = 24000:
	set(value):
		value = clampi(value, 6000, 510000)
		if bitrate != value:
			bitrate = value
			emit_changed()

## Encoder effort, 0 to 10. One voice is cheap to encode, so keep it high.
@export_range(0, 10) var complexity: int = 10:
	set(value):
		value = clampi(value, 0, 10)
		if complexity != value:
			complexity = value
			emit_changed()

@export_group("Silence and Loss")

## Discontinuous transmission: frames Opus classifies as silence aren't
## sent. Opus decides this with its own voice detector; a noisy microphone
## may never count as silent without a noise gate in front of it.
@export var dtx: bool = true:
	set(value):
		if dtx != value:
			dtx = value
			emit_changed()

## Embeds a low-bitrate copy of each frame in the next packet, so a receiver
## can recover single lost packets.
@export var inband_fec: bool = true:
	set(value):
		if inband_fec != value:
			inband_fec = value
			emit_changed()

## Packet loss to prepare for. Higher values spend more bits on redundancy.
@export_range(0, 100, 1, "suffix:%") var expected_packet_loss_percent: int = 10:
	set(value):
		value = clampi(value, 0, 100)
		if expected_packet_loss_percent != value:
			expected_packet_loss_percent = value
			emit_changed()

## Deep REDundancy history to embed in each packet, so receivers can recover
## long bursts of loss. 0 disables it. Only builds with DNN support use it.
@export_range(0, 1000, 10, "suffix:ms") var dred_duration_ms: int = 0:
	set(value):
		value = clampi(value, 0, 1000)
		if dred_duration_ms != value:
			dred_duration_ms = value
			emit_changed()

@export_group("Activity")

## How long after the last sent packet the sender still counts as speaking.
@export_range(0, 2000, 10, "suffix:ms") var speaking_timeout_ms: int = 200:
	set(value):
		value = maxi(value, 0)
		if speaking_timeout_ms != value:
			speaking_timeout_ms = value
			emit_changed()


static func _snap(value: int, valid: Array[int]) -> int:
	var best := valid[0]
	for candidate in valid:
		if absi(candidate - value) < absi(best - value):
			best = candidate
	return best
