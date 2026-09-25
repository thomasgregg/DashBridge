# USB command reference

This is the canonical reference for the USB console implemented by the current
DashBridge firmware. It covers both commands typed by a person and the `db ...`
commands used by the browser setup page.

Connect to a board's USB serial port at **115200 baud, 8 data bits, no parity,
1 stop bit**. End each command with Enter. Close the browser setup page or any
other serial monitor first, because only one program can normally own a serial
port at a time.

## Commands for people

These commands are not case-sensitive. Leading and trailing spaces are ignored.
The firmware rejects non-printable input and lines longer than 32 characters;
an invalid command prints the role-appropriate help text and does nothing else.

| Command | Board A (iPhone) | Board B (Tesla) |
| --- | --- | --- |
| `help` | List available interactive commands | List available interactive commands |
| `status` | Print firmware, iPhone, notification, call, music, contact, board-link, audio-test, and memory state | Print firmware, Tesla, message, call, music, contact, board-link, audio-test, and memory state |
| `pair` | Open iPhone pairing for 120 seconds | Open Tesla pairing for 120 seconds |
| `pair phone` | Open iPhone pairing for 120 seconds | Not available; prints help and changes nothing |
| `pair car` | Not available; prints help and changes nothing | Open Tesla pairing for 120 seconds |
| `test` | Not available; the test must be sent by Board B | Send one test message if the Tesla message channel is ready |

### Pairing without the iPhone app

On Board A, enter `pair phone`. Pair **Dash Messages** from the iPhone first and
allow notification sharing. **Dash Calls** becomes discoverable only after that
notification connection is ready; pair it next in iPhone Settings → Bluetooth.
On Board B, enter `pair car`, pair **Dash Tesla** from the Tesla, and allow
message and contact syncing when offered.

`pair` and the more explicit role command have the same effect on a two-board
image. They open a temporary pairing window; they do not erase saved bonds.

## Call-audio diagnostic commands

Both current board images accept all five commands. The two boards forward a
remote test request over their control link, so either USB console can select
either end. A test starts only during an active call, lasts at most 60 seconds,
and stops on an audio disconnect, session change, or reboot. No command places,
answers, or ends a call.

| Command | Effect |
| --- | --- |
| `audio phone tone` | Send a quiet 1 kHz tone toward the remote caller |
| `audio phone loopback` | Return incoming caller audio toward the remote caller |
| `audio car tone` | Send a quiet 1 kHz tone toward the Tesla |
| `audio car loopback` | Return Tesla microphone audio toward the Tesla |
| `audio off` | Stop the diagnostic mode and restore the normal audio relay |

Use low volume for loopback tests. See [Audio isolation tests](AUDIO_ISOLATION_TESTS.md)
for the exact routes, limits, expected sound, and interpretation of counters.

## Browser setup protocol

The [USB setup page](https://thomasgregg.github.io/DashBridge/setup.html) sends
the commands below. They are documented for diagnostics and compatibility, but
normal users should use the page rather than type them. These commands are
case-sensitive so an iOS application identifier keeps its original spelling.
Responses are single lines beginning with `@DB ` followed by JSON.

| Command | Valid image | Response or effect |
| --- | --- | --- |
| `db status` | A, B | Emit current role, firmware version, and connection/profile readiness as a `status` object |
| `db pair` | A, B | Open that board's pairing window for 120 seconds and emit a `result` |
| `db apps` | A | Emit saved and recently seen application rules as an `apps` object |
| `db discover` | A | Listen for a fresh notification's app for 60 seconds; notification sharing must already be ready |
| `db allow <app-id> <preview>` | A | Allow or update a discovered application and persist the rule |
| `db deny <app-id>` | A | Remove a saved application rule |
| `db test` | B | Send one Tesla test message when its message channel is ready |

For `db allow`, `<app-id>` is an iOS bundle identifier such as
`net.whatsapp.WhatsApp`. The preview value is:

| Value | Forwarded notification content |
| --- | --- |
| `0` | Full content made available by iOS |
| `1` | Sender/title, with the body replaced by “New notification” |
| `2` | Application name only, with the body replaced by “New notification” |

Application identifiers may contain letters, digits, `.`, `-`, and `_` and are
at most 95 characters. Discovery is the normal way to obtain the exact ID; a
valid ID may also be entered directly. At most 12 application rules are stored.
Unsupported or malformed `db ...` requests return a failed `result` and do not
change configuration.

## Commands that deliberately do not exist

There is no USB command to erase all Bluetooth bonds, install firmware, place or
answer a call, fetch arbitrary iPhone messages, or bypass an Apple/Tesla pairing
confirmation. Firmware installation uses the installer or flash helper. A full
merged-image installation may clear bonds; a pairing command does not.
