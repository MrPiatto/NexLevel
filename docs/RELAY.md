# NexLevel relay (works on any network)

Mobile data, many fibre providers and school/office networks use carrier-grade NAT. A PC on those
networks can connect out, but nobody can connect in, so it cannot host a room directly, and
opening router ports does not help. The relay fixes this. The host and the players all connect
*out* to the relay, and the relay passes bytes between them.

- **Private.** Session traffic is end-to-end encrypted (X25519 + XChaCha20-Poly1305). The relay
  only sees encrypted data, and it never sees the level.
- **Short codes.** Relay rooms use 10-character codes like `396ZW-00VNY`.
- **Robust.** If the host's link to the relay drops, the host reclaims the same room within
  90 seconds, and players already in the room stay connected.
- **Hardened.** Per-address connection limits, failed-join rate limiting (to stop room-code
  guessing), handshake/pairing/idle timeouts and a room cap.
- **Light.** One small program with no database. Bandwidth is mostly the level snapshot sent
  once per join, plus small edit messages.

## Running a relay

Fastest way: from this PC run

    tools\relay-upload.ps1 -Server <server IP> -Key <path to the server's SSH key>

It uploads the source, builds it on the server, installs it as a service and opens port 47800.
It also handles the iptables rules on Oracle Cloud images.

Manual way:

Any always-on machine with a public IP address works, such as a small Linux VPS. Open TCP port
**47800**.

- Docker: see `deploy/relay/Dockerfile`.
- Without Docker: see `deploy/relay/nexlevel-relay.service`.

Then set the **Relay server** setting of the mod to the server's address, for example
`relay.example.com` or `203.0.113.10:47800`. Everyone in a room must use the same relay. A
release build can ship with the official address as the default.

## Trying it on one PC

1. Run `tools\bot.ps1 relay` to start a relay on this PC.
2. In GD, set the mod setting **Relay server** to `127.0.0.1`, then click Host room. You get a
   10-character code.
3. In another terminal:
   ```
   $env:NEXLEVEL_RELAY="127.0.0.1"
   tools\bot.ps1 join
   ```

## Many relays (public release)

Room codes carry a relay id (0 to 14; 15 means "the private relay in my settings"). That lets
several relays run side by side, one per region, with more added as the player count grows.

- **Directory.** The mod downloads a small JSON file from the **Relay directory URL** setting and
  caches it. Free static hosting such as GitHub Pages is enough:
  ```json
  {"relays":[{"id":1,"name":"EU-1","host":"eu1.example.com","port":47800},
             {"id":2,"name":"US-1","host":"us1.example.com","port":47800}]}
  ```
  An id must never be reused for a different relay, because open rooms refer to it.
- **Nearest relay.** When a room opens, the host measures the round trip to every relay in the
  directory (Probe/Pong) and uses the fastest one. A player only connects to the relay named in
  the code.
- **Built-in list.** `relay_directory.cpp` ships with the official relays compiled in, used
  until the directory is downloaded.
- **Older builds.** A build that does not know a relay id says "update NexLevel" instead of
  failing silently.


## Public counters
Since NexLevel v1.0.4 the relay answers `StatsRequest` with the number of open rooms and connected players (totals only). The mod shows them in its main-menu menu. **Update the relay** (`tools\relay-upload.ps1`) to enable it; older relays just close the connection and the menu shows "-".

## Public rooms
Since NexLevel v1.0.6 hosts can list their room (`Announce` on the control connection) and the mod asks the relay for the list (`ListRooms` → `RoomList`). **Update the relay** to enable it; older relays close the connection and the list stays empty.
