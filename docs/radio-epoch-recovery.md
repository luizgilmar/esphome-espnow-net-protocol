# ESP-NOW radio epoch recovery

## Problem

ESP-NOW and station Wi-Fi share the ESP32 radio. After an access-point block,
disconnect, or reassociation, the configured encrypted peer can still exist in
the local ESP-NOW table while every reliable delivery acknowledgement times
out. Deleting and adding that peer again is not sufficient in this state.

## Recovery policy

The radio keeps the inexpensive per-peer refresh as the first recovery step.
It also schedules a full radio-epoch restart when either condition is observed:

- the station Wi-Fi association changes; or
- a reliable ESP-NOW delivery acknowledgement times out.

A radio-epoch restart unregisters callbacks, deinitializes ESP-NOW, clears
stale callback queues, waits for the configured Wi-Fi channel to remain stable,
then initializes ESP-NOW and installs every configured encrypted peer again.

## Bounds

- restart delay: 3 seconds plus a deterministic 0–7 second device jitter;
- restart cooldown: 30 seconds;
- restart is deferred while a command or reliable result owns the radio;
- channel stabilization remains mandatory before initialization.

The jitter prevents devices recovering from the same power or network event
from restarting and transmitting simultaneously. The cooldown prevents a bad
association from causing a tight deinitialization loop.

## Diagnostics

Configuration logs expose successful and failed radio recoveries. Runtime logs
identify the recovery reason and the delay selected for that device.
