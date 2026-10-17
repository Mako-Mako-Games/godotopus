@tool
class_name VoicePlaybackSettings
extends Resource
## How a [VoiceReceiver] buffers and decodes a remote voice.
##
## A preset: assigning it to a receiver gives the receiver its own copy. To
## change a running receiver, edit [member VoiceReceiver.settings] (its
## copy); changes apply immediately. Nothing here has to match the sender.

## Audio to buffer before playing. More absorbs more network jitter at the
## cost of latency.
@export_range(0, 500, 5, "suffix:ms") var target_latency_ms: int = 60:
	set(value):
		value = clampi(value, 0, 500)
		if target_latency_ms != value:
			target_latency_ms = value
			max_latency_ms = maxi(max_latency_ms, value)
			emit_changed()

## Buffered audio beyond this is dropped back down to the target, so a stall
## doesn't turn into lasting latency.
@export_range(20, 2000, 10, "suffix:ms") var max_latency_ms: int = 200:
	set(value):
		value = clampi(maxi(value, target_latency_ms), 20, 2000)
		if max_latency_ms != value:
			max_latency_ms = value
			emit_changed()

## Longest run of lost audio to fill in. A longer gap is skipped instead.
@export_range(0, 1000, 10, "suffix:ms") var max_conceal_ms: int = 200:
	set(value):
		value = clampi(value, 0, 1000)
		if max_conceal_ms != value:
			max_conceal_ms = value
			emit_changed()

## 1 or 2. Stereo senders are played as stereo only when this is 2.
@export_enum("Mono:1", "Stereo:2") var channels: int = 1:
	set(value):
		value = 2 if value == 2 else 1
		if channels != value:
			channels = value
			emit_changed()

@export_group("Quality")

## Decoder effort, 0 to 10. On builds with DNN support, 5 and up enables
## neural loss concealment and higher values enable speech enhancement.
@export_range(0, 10) var decoder_complexity: int = 10:
	set(value):
		value = clampi(value, 0, 10)
		if decoder_complexity != value:
			decoder_complexity = value
			emit_changed()

## Reconstructs high frequencies from narrowband speech. Only builds with
## DNN support use it, at [member decoder_complexity] 4 or more.
@export var bandwidth_extension: bool = false:
	set(value):
		if bandwidth_extension != value:
			bandwidth_extension = value
			emit_changed()

@export_group("Activity")

## How long after the last packet the voice still counts as speaking.
@export_range(0, 2000, 10, "suffix:ms") var speaking_timeout_ms: int = 200:
	set(value):
		value = maxi(value, 0)
		if speaking_timeout_ms != value:
			speaking_timeout_ms = value
			emit_changed()
