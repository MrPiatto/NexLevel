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
