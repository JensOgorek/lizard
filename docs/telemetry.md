# Telemetry

Telemetry frames transmit properties and variables in a compact binary format instead of text.
A frame holds the values of one step together with a sequence number, a timestamp and a CRC, and layout lines name and type its fields.
Frames go to the command line and, between microcontrollers, over a [serial bus](#serial-bus) or an [expander](#expander).

## Defining frames

Each call of `core.telemetry` defines one frame with the given properties and variables as fields:

```
core.telemetry(motor.position, motor.axis_state, motor.enabled)
core.telemetry(imu.yaw, core.heap, 1000)
```

Frames are sent at the end of every step, after modules, rules and routines.
An integer as last argument sets an interval in milliseconds (0 to 3600000): the frame is then only sent in steps at least that long after its previous frame.
An `int` variable or property in that place counts as a field, not as the interval.

Each field is a single property or variable, including nested properties like `arm.motor.position`, and is sent with a fixed type:

| Data type | Type | Encoding                                    |
| --------- | ---- | ------------------------------------------- |
| `float`   | `f`  | 32-bit float                                |
| `int`     | `i`  | 32-bit signed integer, clamped to its range |
| `bool`    | `?`  | 1 bit                                       |

Strings and other expressions cannot be fields.
The payload of a frame holds at most 180 bytes, e.g. 45 numbers.
Frame IDs count up from 1 in the order of definition.
A call with the same fields and interval as an existing frame of the same caller defines no new frame, but sends its layout lines again.

`core.clear_telemetry()` removes the frames of its caller: over a serial bus the frames that the calling node ordered, otherwise all frames that were not ordered over a bus.
`core.telemetry_info()` sends the layout lines of all frames again.
Layout lines are sent at the end of a step, `core.telemetry_info_rate` lines per step (default: 4), so that a long layout does not delay a single step; 0 sends them all at once.
A new frame is sent for the first time once all its layout lines are out, so a reader never gets a frame before its layout.
`core.pause_broadcasts()` pauses frames as well until `core.resume_broadcasts()`.

For startup scripts written for an earlier frame format, `core.frame()`, `core.frame_add()` and `core.frame_clear()` are accepted and ignored with a warning.

## Format

A frame is the line `~<base64>@xx`: a tilde, the Base64 encoding of the frame body (standard alphabet with `=` padding) and the line [checksum](machine_safety.md#checksums).

| Bytes | Content                                             |
| ----- | --------------------------------------------------- |
| 1     | Frame ID                                            |
| 1     | Sequence number, counting the frames modulo 256     |
| 4     | `core.millis` of the sender when it built the frame |
| n     | Payload                                             |
| 2     | CRC-16/CCITT-FALSE of all preceding bytes           |

Multi-byte values are little-endian.
The payload holds the `float` and `int` fields in their order, followed by the `bool` fields as bits in their order, starting with the least significant bit and padded with zeros to a full byte.
A gap in the sequence numbers of a frame ID means that frames were lost.

The layout of a frame comes as one line per field:

```
__LAYOUT__v1 <frame>.<index>/<count> <name>:<type>
```

| Part      | Meaning                                                                      |
| --------- | ---------------------------------------------------------------------------- |
| `v1`      | Format version                                                               |
| `<frame>` | Frame ID                                                                     |
| `<index>` | Position of the field in the frame, starting at 0                            |
| `<count>` | Number of fields in the frame, so that a missing last layout line is noticed |
| `<name>`  | `module.property`, `module.property.subproperty` or the name of a variable   |
| `<type>`  | `f`, `i` or `?` as above                                                     |

Layout lines are sent when a frame is defined and on `core.telemetry_info()`.
For example, `core.telemetry(motor.position, motor.axis_state, motor.enabled)` for an [ODrive motor](module_reference.md#odrive-motor) sends these layout lines and frames like the one in the last line:

```
__LAYOUT__v1 1.0/3 motor.position:f@6e
__LAYOUT__v1 1.1/3 motor.axis_state:i@52
__LAYOUT__v1 1.2/3 motor.enabled:?@49
~AQdA4gEAtvOdPwgAAAABwBc=@5c
```

The frame body `01 07 40e20100 b6f39d3f 08000000 01 c017` holds frame ID 1, sequence number 7, millis 123456, the values 1.234, 8 and `true`, and the CRC.

## Serial bus

A [serial bus](module_reference.md#serial-bus) peer defines its frames like any node, in its startup script or at its command line:

```
serial = Serial(3, 1, 460800, 2)
bus = SerialBus(serial, 2)
core.telemetry(motor.position, motor.enabled, parked, 100)
```

Once a coordinator polls the peer, the peer keeps the newest frame of each ID and sends it with the next poll, so a poll delivers the current values and no backlog; a replaced frame does not count in the sequence numbers.
Before the first poll its frames and layout lines go to its own command line; with the first poll it announces its layouts again, this time to the coordinator.
It keeps at most 8 frames per bus and warns about any further one, which it does not send.

The coordinator checks the CRC of every frame and passes frames and layout lines on to its command line with the sender in front, like echo lines:

```
bus[2]: __LAYOUT__v1 1.0/3 motor.position:f@xx
bus[2]: ~AQdA4gEAtvOdPwgAAAABwBc=@5c
```

A host decodes them with the sender as the key, as `telemetry.py` does (see below).
`core.telemetry_info()` on the coordinator also asks every peer for its layout lines, so a host that connects later gets them the same way.
Frames never take the last slots of the bus's inbound queue; frames dropped for that reason, e.g. while the coordinator runs its startup, are counted in `telemetry_dropped`.

A coordinator that needs a value of a peer itself, e.g. in a rule, declares it in a [bus telemetry](module_reference.md#bus-telemetry) module:

```
bus = SerialBus(serial, 1)
bus.make_coordinator(2)
arm = BusTelemetry(bus, 2)
bool arm.parked = false
when arm.parked == false then wheels.locked = true end
```

The module looks for its declared names in the layout lines of node 2 and copies the fields out of every frame that passes by; nothing is ordered from the peer, which has to send the value in one of its own frames.
Until a frame with the value arrives, the property keeps its start value and `arm.age` counts up, so a rule sees a missing value like a dead peer.
If frames of the peer arrive but none of them carries a declared name within 5 seconds, the module says so once.
When a frame arrives whose layout the coordinator does not have, e.g. because it booted after the peer, it asks the peer for its layout lines, at most every 2 seconds; when the peer reports `Ready.`, the coordinator forgets its layouts.
A peer whose layout lines carry another format version is reported once; its frames are still passed on, but not mirrored until the peer boots again.
Bus payloads that start with `~` or `__LAYOUT__` are taken as telemetry and neither printed as commands nor interpreted as Lizard code.

## Expander

An [expander](module_reference.md#expander) whose `telemetry_interval` is 0 or more gets the properties of its proxies as frames instead of text broadcasts:

```
expander = Expander(serial, 32, 33)
expander.telemetry_interval = 100
led = expander.Output(15)
```

The setting applies to proxies created afterwards, so it belongs right after the expander.
In the step after proxies are created, the expander orders the `bool`, `int` and `float` properties that the module types of its proxies have by default, except `is_ready`, as frames every `telemetry_interval` milliseconds (0: every step).
The order starts with `core.clear_telemetry()`, which also removes frames that the other microcontroller defined itself.
String properties and properties that a module adds at runtime, like the `offset_<id>` of a serial bus, are not ordered.
If no layout line arrives within five seconds, e.g. because the other microcontroller runs a firmware without telemetry, the expander prints a warning, sends `core.clear_telemetry()`, switches the proxies to text broadcasts and sets `telemetry_interval` to -1.
If a frame arrives whose layout is incomplete, the expander asks for the layout lines again, at most every 2 seconds.
Lines from the other microcontroller that start with `~` or `__LAYOUT__` are taken as telemetry and not printed.

## Host decoding

`monitor.py` decodes frames with the layout lines it has received and prints them with the field names:

```
[frame 1 seq=7 millis=123456] motor.position=1.234 motor.axis_state=8 motor.enabled=true
```

A frame it cannot decode is printed with its status and its payload in hex, e.g. `[frame 1 seq=7 millis=123456 no_layout] b6f39d3f0800000001`.

| Status              | Meaning                                          |
| ------------------- | ------------------------------------------------ |
| `no_layout`         | No layout line of the frame has arrived yet      |
| `layout_incomplete` | Layout lines of some fields are missing          |
| `layout_mismatch`   | The payload size does not match the layout       |
| `unknown_version`   | The sender uses a format version other than `v1` |

A line is only taken as a frame if it is valid Base64 with a matching CRC, so other lines starting with `~` and lines with a wrong checksum are printed as text.
A host that starts reading while frames are already running gets their layout lines with `core.telemetry_info()`.

`telemetry.py` decodes a recorded log in the same way and prints all other lines unchanged:

```bash
./telemetry.py [<logfile>] [--csv <dir>] [--stats] [--non-strict]
```

| Argument       | Description                                                                    |
| -------------- | ------------------------------------------------------------------------------ |
| `logfile`      | Log file to read (default: standard input)                                     |
| `--csv`        | Write one CSV file per frame ID and layout into the given directory            |
| `--stats`      | Print the frame counters to standard error at the end                          |
| `--non-strict` | Also decode frames whose layout has gaps (only for layout lines without count) |

The CSV file `frame_<id>.csv` has a column `millis` and one column per field; a new layout of the same frame ID starts `frame_<id>_2.csv` and so on.
The counters of `--stats` list per frame ID the frames, the missing sequence numbers, the restarts of the sender (`millis` going back by more than a second) and the frames per status.

In Python, `telemetry.Decoder` does the same for any source of lines: `feed(line, sender)` takes a line without line end and checksum and returns a `Text`, `Layout` or `Frame`, whose `values` map the field names to their values.
Layouts are kept per `sender`, and `check_line()` strips and checks the line checksum.
