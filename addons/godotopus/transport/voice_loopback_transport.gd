class_name VoiceLoopbackTransport
extends VoiceTransport
## Delivers every packet sent through it back to itself.
##
## For tests, demos and hearing yourself: connect a [VoiceSender] and a
## [VoiceReceiver] to the same loopback transport. It can simulate a bad
## network with loss, delay and jitter (jitter larger than the frame
## duration also reorders packets).

## Sender id reported with each delivered packet.
@export var peer_id: int = 1

@export_range(0.0, 100.0, 0.1, "suffix:%") var loss_percent: float = 0.0
## Fixed delay added to every packet.
@export_range(0, 2000, 1, "suffix:ms") var delay_ms: int = 0
## Random extra delay per packet, from 0 to this.
@export_range(0, 1000, 1, "suffix:ms") var jitter_ms: int = 0
## Seed for loss and jitter. 0 picks a random seed.
@export var random_seed: int = 0:
	set(value):
		random_seed = value
		if random_seed == 0:
			_rng.randomize()
		else:
			_rng.seed = random_seed

## Packets passed to [method send], and how many of those were dropped.
var packets_sent: int = 0
var packets_lost: int = 0

var _rng := RandomNumberGenerator.new()
var _time := 0.0
# Packets in flight, ordered by delivery time: [time, packet, sequence].
var _in_flight: Array[Array] = []


func _init() -> void:
	process_mode = PROCESS_MODE_ALWAYS
	_rng.randomize()


func _process(delta: float) -> void:
	advance(delta)


func send(packet: PackedByteArray, sequence: int) -> void:
	packets_sent += 1
	if loss_percent > 0.0 and _rng.randf() * 100.0 < loss_percent:
		packets_lost += 1
		return
	var delay := delay_ms
	if jitter_ms > 0:
		delay += _rng.randi_range(0, jitter_ms)
	if delay <= 0:
		packet_received.emit(peer_id, packet, sequence)
		return
	var entry := [_time + delay / 1000.0, packet, sequence]
	var index := _in_flight.bsearch_custom(entry, func(a, b): return a[0] < b[0], false)
	_in_flight.insert(index, entry)


## Moves the transport's clock forward and delivers packets that are due.
## Called every frame; call it yourself to step time in tests.
func advance(seconds: float) -> void:
	_time += seconds
	while not _in_flight.is_empty() and _in_flight[0][0] <= _time:
		var entry: Array = _in_flight.pop_front()
		packet_received.emit(peer_id, entry[1], entry[2])


## Packets still waiting for their delivery time.
func get_in_flight_count() -> int:
	return _in_flight.size()
