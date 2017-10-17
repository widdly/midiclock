# midiclock
```
  Receive OSC messages on localhost..  /tempo i, /start, /stop, /continue 
  Send MIDI clock messages via ALSA midi port

Usage: 
  midiclock [-p|--port PORT]
            [-r|--resolution PPQ]
            [-s|--start] 
            [-t|--tempo BPM]

Options:
  -h, --help         This message
  -p, --port         OSC receive port number, default 4040
  -r, --resolution   Tick resolution per quarter note (PPQ), default 120
  -s, --start        Start MIDI clock automatically, default off
  -t, --tempo        Speed, in BPM, default 100
```
```
Requires alsa and lo libraries.

Build with...
gcc -o midiclock midiclock.c -lasound -llo
```
