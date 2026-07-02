extends RefCounted
class_name JitterBuffer

## Receive-side playout buffer for ONE remote peer's voice stream.
##
## Deviations from a generic "reorder + clock-drift" jitter buffer, and why:
##
## 1. No reorder map. The packets are sent over an "unreliable_ordered" RPC
##    channel, which means Godot's MultiplayerAPI already discards anything
##    that arrives late or out of order at the transport layer — we will only
##    ever see sequence numbers increase, with gaps (drops), never reordering.
##    A SortedMap-based reorder buffer would be solving a problem that can't
##    actually occur here, so this just tracks one expected sequence number.
##
## 2. No wall-clock latency math. Peers are not clock-synchronized, so
##    "latency = my_clock - their_timestamp" is comparing two unrelated
##    clocks and will silently drift or jump on any clock-skew. Instead this
##    buffer treats backlog depth (how many undecoded packets are queued) as
##    the only signal — that's a purely local, self-consistent measurement.
##
## 3. Real FEC integration. On a detected gap, the frame immediately
##    preceding the next available packet gets a real attempt at recovery via
##    that packet's in-band FEC data before falling back to plain
##    concealment; earlier frames in a longer gap go straight to concealment
##    since FEC can only ever recover the immediately-preceding frame.

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

## Call when the sender announces a new speech burst (stream_id changed) or
## after a config hot-reload. Drops anything in flight so gap-detection and
## FEC don't try to reach across a discontinuity that isn't a real packet loss.
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
		#
		# NOTE: this branch used to be checked with the unsigned
		# _seq_distance() helper, which can never return a negative number by
		# construction — so this case was silently unreachable. A stale/late
		# packet fell through into the gap-handling branch below instead,
		# where the wraparound distance from _expected_seq back to a seq
		# that's actually BEHIND it comes out as a huge number (up to ~65535),
		# which would have tried to synthesize tens of thousands of PLC
		# concealment frames in a single tick. _signed_seq_diff() below
		# distinguishes "behind" (negative) from "ahead" (positive) instead
		# of always measuring forward distance.
		_raw_queue.pop_front()
		return

	# There's a gap: frames [_expected_seq, seq - 1] are missing. The one
	# immediately before this packet gets a real FEC recovery attempt; any
	# earlier ones in the same gap get plain concealment, since FEC can only
	# ever reach back one frame.
	var missing := diff
	for i in range(missing - 1):
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
## negative means it's behind (stale/duplicate). Using a half-range signed
## result instead of always measuring the forward distance is what lets a
## packet that's actually behind be told apart from one that's merely due to
## a legitimate gap — the two look identical to a plain unsigned distance.
func _signed_seq_diff(to_seq: int, from_seq: int) -> int:
	var d := (to_seq - from_seq) % SEQ_MODULO
	if d > SEQ_MODULO / 2:
		d -= SEQ_MODULO
	elif d < -SEQ_MODULO / 2:
		d += SEQ_MODULO
	return d
