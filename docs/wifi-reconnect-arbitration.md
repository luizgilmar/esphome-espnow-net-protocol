# WiFi reconnect arbitration

`espnow_net_protocol` opts in to the generic reconnect-suppression API supplied
by `esphome-wifi-espnow-arbitration`. The WiFi component owns driver mechanics;
this component owns the ESP-NOW availability policy.

When STA association is lost, each device waits 10–30 seconds before requesting
the configured ESP-NOW channel. It then keeps WiFi reconnect activity suppressed
for 30–60 seconds and opens an 8-second reconnect window. The cycle repeats
until WiFi associates again. Both long phases use deterministic MAC-derived
jitter with different salts, avoiding synchronized behavior after a shared
power failure.

An established WiFi connection immediately ends arbitration. Every accepted
suppression request is paired with exactly one release. The policy never calls
ESP-IDF WiFi scan, disconnect, channel, stop, or deinitialization APIs directly.

