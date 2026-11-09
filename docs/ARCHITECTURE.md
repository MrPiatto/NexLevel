# NexLevel: how the code works

A guide to the whole project, from the big picture down to the details. Each section says **what** a
part does, **why** it is built that way and **where** it lives.

---

## 1. The idea in one sentence

One player (the **host**) opens a **room** from their editor. The others (the **clients**) join with a
code. Every change to an object becomes an **operation** (op): the host puts all ops in **one global
order** and sends them back to everyone, so every editor applies the same changes in the same order
and the level stays identical for all players.

---

## 2. Folder layout

| Folder | Contents |
|---|---|
| `core/` | The engine: networking, sync, encryption, relay. **It knows nothing about Geometry Dash**: plain C++20, built and tested on its own. |
| `compat/gd2208-geode5/` | The actual mod for GD 2.2081 / Geode v5: editor hooks, UI, saving. Uses the core. |
| `compat/gd2209-geode6/` | Placeholder for the future GD 2.209 port. |
| `deploy/relay/` | Scripts to install the relay server on a VPS (Docker, systemd service). |
| `tools/` | PowerShell scripts: build, tests, test bot, relay upload. |
| `docs/` | Guides (online play, relay, test plan, this file). |

**Why the core is separate:** it can be tested without opening the game (358 automated checks), and
when a new GD version comes out only the `compat/` part has to be rewritten.

---

## 3. Data model (core/sync)

- **NetId** (`netid.hpp`): the room-wide id of an object. A 64-bit number = *player id* + *counter*.
  Each player creates ids in their own range, so two players never create the same id. The player
  part also tells who placed an object ("Placed by").
- **PropMap** (`object_props.hpp`): a GD object is a *key → value* map, the same format GD uses in
  saved levels (`1,1,2,15,3,45` = object 1, x=15, y=45). `parseSaveString` / `toSaveString` convert
  between text and map; `diffProps` finds what changed.
- **Op** (`op.hpp`): three kinds: **Create** (new object with its properties), **SetProps** (only the
  changed properties), **Delete**.
- **Replica** (`replica.hpp`): the level state = all objects + "tombstones" of deleted objects. Rules:
  a Create on an existing or deleted id is ignored, a SetProps on a missing object is ignored. So
  applying the same ops in the same order always gives the same result (determinism).
- **HostSequencer** (`host_sequencer.hpp`): on the host, gives every op a global **sequence number**.
  It is the referee of the order.
- **ClientReplica** (`client_replica.hpp`): on a client, keeps the state **confirmed** by the host
  plus the local ops **still waiting** for confirmation. What is shown = confirmed + pending. When the
  confirmation arrives the op moves from pending to confirmed; if the host rejects it, it is dropped
  and the object goes back to the confirmed state. Players see their own edits immediately without
  waiting for the network (*optimistic update*).
- **Level settings** (`level_settings.hpp`): colours, background, ground, song, etc. travel as **one
  special object** with a reserved id (`kLevelSettingsId`), so they use the same mechanism as objects.
  Each setting is a separate property, so two people changing different settings never overwrite
  each other.
- **Level string** (`level_string.hpp`): builds a GD level string (`header;object;object;...`) from a
  Replica. Used to save the room and to open it with GD's own loader.

---

## 4. Networking (core/net, core/frame, core/session)

- **Asio** (standalone) runs the TCP sockets on a separate **network thread**. The game never blocks
  waiting for the network.
- **Frame** (`frame.hpp`): every message on a connection is a frame = header (magic, protocol
  version, type, length) + payload. `FrameDecoder` rebuilds frames from bytes arriving in pieces and
  refuses oversized or malformed ones.
- **Messages** (`session/messages.hpp`): Hello (who I am), Welcome (your id), SnapshotChunk(Z)/End
  (the level when you join), Submit (a client op), Sequenced (an op ordered by the host), Roster
  (player list), Ping/Pong, Presence, PlayerState, Chat, etc. Each message has `encode`/`decode` with
  validity checks: a corrupted message is refused, never executed.
- **ByteWriter/ByteReader** (`bytes.hpp`): bounds-checked little-endian numbers and strings: reading
  past the end does not crash, it marks an error.
- **Protocol version** (`version.hpp`, currently **v8**): if host and client differ, the connection is
  refused with a clear message.

### Host (`session/host.cpp`)
1. Opens a TCP port (47777) and accepts connections.
2. On Hello it checks GD version, room code and player count.
3. Sends Welcome + a **snapshot** of the level + SnapshotEnd, then every new op in order.
4. Every received op (Submit) goes through the sequencer and is sent to everyone (Sequenced).
5. **Abuse limits**: a *token bucket* limits how many ops per second a client can send; pings and
   chat lines are rate limited; view-only players' ops are rejected.

### Client (`session/client.cpp`)
1. Tries **several destinations in parallel** (direct, local network, relay) and keeps the first that
   answers.
2. Receives the snapshot, then applies incoming ops.
3. If the connection drops, it **reconnects** on its own with a *resume token*: the host knows which
   ops it already received, so nothing is lost.

### Compressed snapshot (`compress.hpp`)
When someone joins, the level is split into 512 KB chunks and each chunk is **compressed** with an
LZ77 algorithm (LZ4 block format): it finds byte sequences already seen and replaces them with "copy
N bytes from M positions back". GD data is very repetitive, so it shrinks about 7 times. The
decompressor checks every length and offset, so corrupted data is refused.

---

## 5. Encryption (core/crypto)

- Library: **Monocypher**.
- Each side generates an **X25519** key pair. They exchange public keys (Hello / Secure) and compute
  the same **shared secret** (Diffie-Hellman).
- Keys are derived from the secret with **BLAKE2b**.
- Every following message is encrypted with **XChaCha20-Poly1305**: encryption + authentication (a
  modified message is discarded). The nonce is an implicit counter, so a replayed or reordered message
  is not accepted.
- Result: the relay and anyone in between only see encrypted data.
- Random numbers come from the operating system: `BCryptGenRandom` on Windows, `getrandom` on Linux,
  `arc4random_buf` on macOS.

---

## 6. Connecting without IPs (codes, UPnP, relay)

- **Room code** (`room_code.cpp`): packs everything needed to find the room (address or relay room,
  port, key, checksum) into a few characters. Three formats: 13 characters (local network), 15
  (direct internet), **10 (relay)**. The checksum rejects mistyped codes.
- **UPnP** (`net/upnp.cpp`): the host asks the router to open the port automatically (SSDP to find the
  router, SOAP to request the mapping). Handles lease renewal, port conflicts and routers that don't
  support it. It cannot work behind CGNAT (mobile data), which is why the relay exists.
- **Relay** (`relay/`, server EU-1 on AWS): a public server acting as a bridge. The host opens a room
  on the relay and gets a number; the client connects to the relay with that number and the relay
  forwards bytes between the two (encrypted, so the relay cannot read them). Per-IP limits, timeouts,
  and a 90-second grace period if the host drops for a moment. The host pings the relay every second
  and shows that round trip in its Network tab.
- **Several relays**: the mod measures the latency to every relay in the list (`probeRelays`) and uses
  the fastest. The list can be updated from a URL without updating the mod.

---

## 7. The mod in Geometry Dash (compat/gd2208-geode5/src)

### `editor_tracker.cpp`: the heart of editor sync
- Uses Geode **hooks** (`$modify`) on editor functions: create, delete, move, rotate, scale, flip,
  undo/redo, playtest, touches.
- **Detecting local changes**: keeps a copy of each object's properties (*cache*). Every frame it
  compares the touched objects with the cache; if something changed it sends a SetProps with only the
  differences. A **sweep** checks 100 objects per frame to catch changes no hook saw.
- **Applying other players' changes** (`applyRemote`): creates, replaces or deletes objects in the
  editor, ignoring its own hooks meanwhile so it doesn't send the same change back.
- **Crash safety**:
  - every tracked object is `retain()`ed, so a stored pointer can never point to freed memory;
  - while you are in the middle of a gesture (mouse/touch held, swipe, **Shift**) or an edit popup is
    open, other players' changes that remove or replace objects **wait**, and your own are sent
    together at the end;
  - an object is deselected and taken out of its linked group before it is removed;
  - the exit of a teleport portal (749) is never synced on its own: GD manages it with its entry (747).
- **Joining / re-entering** (`adoptFromSession`): instead of deleting and recreating everything, it
  pairs the objects already in the editor with the room's objects **by content** (same property
  string) and only creates or removes the differences.
- **Playtest spectating**: during a playtest it sends position, game mode, icon and colour ~30 times
  per second (`PlayerState`).

### `session_manager.cpp`: the session for the whole game
Keeps the room (host or client) alive outside the editor too, calls `pump()` every frame, shows
notices (joined, left, reconnected...), picks the code to show (relay > internet > local network),
attaches/detaches the editor, writes the room into the level before the host re-enters the editor,
and opens the editor for players who joined from the main menu once the level has arrived.

### `autosave.cpp`: no lost work
- The host saves the room into its level every 90 s (5 min for huge levels) when it changed, even
  from the menus: the level string is rebuilt from the Replica (`buildLevelString`) and compressed
  like GD does (`ZipUtils::compressString`).
- Also saves when the host leaves the editor, closes the room, and whenever GD saves the game
  (`AppDelegate::trySaveGame` hook).
- Players keep a backup copy "NL - <host>".
- Quitting GD while hosting shows a warning first (`MenuLayer::onQuit` hook).

### `features.cpp`: collaboration extras
- **Chat**: `Chat` message (client → host → others). The host sets the real sender id, strips control
  characters, caps lines at 200 characters and one line every 300 ms per player.
- **View-only players**: the host keeps a set of view-only players; their ops are rejected
  (`Rejected`), so the edit is undone in their editor. The flag travels in the Roster.
- **Restore points**: a copy of the room as its own local level "<name> CP hh:mm" (button, or every
  10 minutes when the room changed; not automatic on huge levels).
- **Go to player**: moves the editor camera to the player's presence position.
- **Crash report prompt**: an "in-room.marker" file exists while you are in a room; if it is still
  there at the next start, GD closed unexpectedly and NexLevel offers to open the logs and the bug
  report page.

### Other files
| File | What it does |
|---|---|
| `level_settings_adapter.cpp` | Reads and writes level settings (44 fields, colours, song). |
| `presence_overlay.cpp` | Draws each player's view outline, playtest icons and trails. |
| `nexmenu.cpp` | The NexLevel menu (Room, Players, Chat, Network, Settings) and the main-menu button. |
| `ui_kit.cpp` | Menu graphics: rounded rectangles, gradients, buttons, ping chart, quality gauge. |
| `ping_hud.cpp` | The editor label (ping / hosting / view only / placed by). |
| `hosting_badge.cpp` | The "Hosting NexLevel room" badge outside the editor (Geode `OverlayManager`). |
| `relay_directory.cpp` | Relay list (built-in + downloaded from the URL). |

---

## 8. Ping and connection quality (`session/ping.cpp`)
A Ping is sent every second; whoever answers also reports how long it waited before answering, which
is subtracted, so only network time is measured. The last 60 samples are kept: average, **jitter**
(how much it varies) and **loss**. Score = average + 2 × jitter + 10 × loss%, which gives the bands
Demon / Insane / High / Normal / Bad.

---

## 9. Tests and builds
- `tools\test-core.ps1`: builds and runs the core tests (sync with several simulated clients,
  reconnection, encryption, relay, UPnP with a fake router, compression, chat, view-only...).
- The tests were also run under **AddressSanitizer** (memory errors) and **ThreadSanitizer** (thread
  errors).
- GitHub Actions rebuilds and tests the core on Windows, Linux and macOS on every push, builds the mod
  for Windows and, with a `vX.Y.Z` tag, publishes the release with the `.geode` file.
- `tools\bot.ps1`: a command-line fake player that joins a room, places blocks and simulates a
  playtest, to test without a second PC.

---

## 10. Common questions, short answers

- **Why a host that orders ops instead of peer-to-peer?** It is the simplest way to get the same order,
  and therefore the same level, for everyone. No conflicts to resolve afterwards.
- **What if two people edit the same object?** The later op in the host's order wins (*last writer
  wins*), per property.
- **Why TCP and not UDP?** Ops must all arrive, in order. TCP guarantees that.
- **Why is a relay needed?** On mobile data and with many providers (CGNAT) a device cannot accept
  connections from outside; the relay is a public point both sides can reach.
- **Can the relay read the level?** No, it only forwards end-to-end encrypted data.
- **Why were there heap-corruption crashes?** GD keeps raw pointers to objects; removing one while GD
  is using it (gesture in progress, popup open, linked portal) leaves a pointer to freed memory. That
  is why the `retain`, the waiting during gestures and the portal handling exist.
- **Why is joining big levels fast?** Compressed snapshot (about 7×) + level opened with GD's own
  loader + pairing by content instead of recreating everything.
