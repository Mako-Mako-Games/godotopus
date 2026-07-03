extends RefCounted
class_name JitterBuffer


var decoder: GodotOpusDecoder
var target_depth_frames: int = 3
var max_buffered_frames: int = 10

var _expected_seq: int = -1
var _raw_queue: Array[Dictionary] = [] # {seq: int, bytes: PackedByteArray}
var _decoded_queue: Array[PackedFloat32Array] = []
var _primed: bool = false

const SEQ_MODULO := 65536

func configure(p_decoder: GodotOpusDecoder, p_target_depth_frames: int, p_max_buffered_frames: int) -> void:
	decoder = p_decoder
	target_depth_frames = max(1, p_target_depth_frames)
	max_buffered_frames = max(target_depth_frames + 1, p_max_buffered_frames)

## Call after a config hot-reload, or any time the decoder itself was
## recreated. Drops anything in flight and resets the decoder's internal
## state so a stale sequence/decoder state from before doesn't leak in.
##
## NOT needed for normal speech pauses — since silence is simply not
## transmitted at all (see VoiceConfig.use_dtx), there's no sequence gap or
## decoder discontinuity to reset across in the first place. A genuinely
## out-of-nowhere sequence jump (e.g. the sender reconnected) is instead
## handled inline by _decode_one_step()'s resync guard below, without needing
## an explicit signal from the sender.
func reset() -> void:
	_expected_seq = -1
	_raw_queue.clear()
	_decoded_queue.clear()
	_primed = false
	if decoder:
		decoder.reset()

func insert(opus_bytes: PackedByteArray, sequence: int) -> void:
	_raw_queue.append({"seq": sequence, "bytes": opus_bytes})

## Call once per process tick. Decodes as many packets as the catch-up policy
## allows this tick and appends the resulting PCM to the decoded queue.
func process_tick() -> void:
	if _raw_queue.is_empty():
		return

	if not _primed:
		if _raw_queue.size() < target_depth_frames:
			return # still warming up — absorb arrival jitter before playing anything
		_primed = true
		_expected_seq = _raw_queue[0]["seq"]

	if _raw_queue.size() > max_buffered_frames:
		# Backlog is badly out of control (e.g. we briefly stalled). Jump the
		# expected sequence to the newest packet instead of grinding through
		# the whole backlog one concealed frame at a time, which would just
		# add latency in the name of "catching up".
		var newest: Dictionary = _raw_queue[_raw_queue.size() - 1]
		_expected_seq = newest["seq"]

	# Catch-up policy: decode an extra packet this tick if backlog is above
	# target, instead of ever dropping audio outright. Bounded latency without
	# audible gaps, as long as the backlog isn't catastrophically large (handled above).
	var decodes_this_tick := 2 if _raw_queue.size() > target_depth_frames else 1
	for i in range(decodes_this_tick):
		if _raw_queue.is_empty():
			break
		_decode_one_step()

func _decode_one_step() -> void:
	var packet: Dictionary = _raw_queue[0]
	var seq: int = packet["seq"]

	if seq == _expected_seq:
		_raw_queue.pop_front()
		_decoded_queue.append(decoder.decode(packet["bytes"]))
		_expected_seq = _wrap(_expected_seq + 1)
		return

	var diff := _signed_seq_diff(seq, _expected_seq)
	if diff < 0:
		# Stale/duplicate (shouldn't happen over unreliable_ordered, but don't
		# let it wedge the expected-sequence tracking if it somehow does).
		_raw_queue.pop_front()
		return

	# Sanity guard: a gap this large isn't real jitter or packet loss (those
	# are bounded by max_buffered_frames), it's a sign the sequence space
	# itself reset out from under us (sender reconnected, restarted, etc).
	# Concealing a "gap" that size would be both nonsensical and expensive —
	# just treat this packet as the start of a fresh sequence instead. This
	# replaces the old reliable-RPC-driven stream reset, without needing any
	# explicit signal from the sender at all.
	var resync_gap_threshold := max_buffered_frames * 4
	if diff > resync_gap_threshold:
		_raw_queue.pop_front()
		_decoded_queue.append(decoder.decode(packet["bytes"]))
		_expected_seq = _wrap(seq + 1)
		return

	# There's a real gap: frames [_expected_seq, seq - 1] are missing. Try
	# Deep Redundancy (DRED) first for the ones far enough back that plain
	# FEC can't reach them — it's a harmless no-op (returns 0) if the packet
	# has none, or this build doesn't support it, in which case we fall back
	# to plain concealment exactly as before. The one immediately before this
	# packet still gets FEC, which is cheaper and always available when the
	# sender has in-band FEC enabled.
	var missing := diff
	var dred_samples_back := decoder.parse_dred(packet["bytes"])
	var frame_size := decoder.get_frame_size()

	for i in range(missing - 1):
		var frames_before := missing - i
		var needed_samples := frames_before * frame_size
		if dred_samples_back >= needed_samples:
			_decoded_queue.append(decoder.decode_dred(needed_samples))
		else:
			_decoded_queue.append(decoder.decode_plc())
	_decoded_queue.append(decoder.decode_fec(packet["bytes"]))

	_raw_queue.pop_front()
	_decoded_queue.append(decoder.decode(packet["bytes"]))
	_expected_seq = _wrap(seq + 1)

func has_ready_frames() -> bool:
	return not _decoded_queue.is_empty()

## Pulls and clears all currently decoded frames. Caller is expected to push
## these straight into one or more AudioStreamGeneratorPlayback instances.
func pull_ready_frames() -> Array[PackedFloat32Array]:
	var out := _decoded_queue.duplicate()
	_decoded_queue.clear()
	return out

func _wrap(v: int) -> int:
	return ((v % SEQ_MODULO) + SEQ_MODULO) % SEQ_MODULO

## Signed circular distance from `from_seq` to `to_seq`, in (-MODULO/2, MODULO/2].
## Positive means `to_seq` is ahead of `from_seq` (a gap of that many frames);
## negative means it's behind (stale/duplicate).
func _signed_seq_diff(to_seq: int, from_seq: int) -> int:
	var d := (to_seq - from_seq) % SEQ_MODULO
	if d > SEQ_MODULO / 2:
		d -= SEQ_MODULO
	elif d < -SEQ_MODULO / 2:
		d += SEQ_MODULO
	return d
