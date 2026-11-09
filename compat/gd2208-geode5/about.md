# NexLevel

**Create Together. Build Beyond.** by Forg3 Studios

Build Geometry Dash levels with your friends in real time, in the same editor.

## How to play
- **Host:** open a level in the editor, press **Esc** > **NexLevel Collab** > **Host room** (this level or a safe copy). Copy the code and send it to your friends.
- **Join:** tap the **NexLevel logo** at the top right of the main menu, paste the code and press **Join**. The room opens in a new level, your own levels are never touched.

No IP addresses, no router settings.

## What is shared
- **Every object:** placing, moving, rotating, scaling, colouring and deleting.
- **Level settings:** colour channels, background, ground, fonts, start options, guidelines and the song.
- **Everyone's view:** a thin coloured outline with each player's name shows where they are working.
- **Playtests:** when someone playtests, the others watch their icon play right in the editor, with a trail in that player's colour.

## The NexLevel menu
Tabs: **Room** (code, players, objects, uptime,), **Players** (ping, profiles, Go to player, view-only, kick), **Chat**, **Network** (connection quality, live ping chart) and **Settings**.

## Rooms
- Up to 16 players. The host can kick players, make them view-only and close the room.
- If the host leaves the editor, the room stays open and everyone keeps building.
- Dropped connections reconnect on their own, without losing work.
- Autosave into the host's level while the room changes. Only the host keeps the level: players can't save someone else's room.

## Privacy and online connections
NexLevel does **not** collect, upload or sell any data, and has no analytics.
- **Who sees what:** the people in your room see your GD username, your GD account ID (for the profile button), where you are looking in the editor, your playtest icon and your chat messages. Nothing is shared outside your room.
- **Relay servers:** to work on any network (including mobile data), rooms go through a Forg3 Studios relay server (official list, or your own one set in the settings). The relay only forwards **encrypted** data: it cannot read levels, names or chat. Like any server, it sees your IP address to connect you; IPs are used only in memory for anti-abuse limits and are never logged or stored; the relay log has connection counts only.
- **Direct connections:** without a relay, players connect to the host directly (UPnP or same Wi-Fi), so the host's and players' IPs are visible to each other's computers, as in any online game. Your IP is hidden in the menu until you tap Show.
- **Encryption:** all room traffic is end-to-end encrypted (X25519 + XChaCha20-Poly1305).
- **Community numbers:** the main-menu NexLevel menu reads public counters (downloads on GitHub / the Geode index, players and rooms on the relays). They are totals only: no names or IPs.
- **Update check:** at startup NexLevel asks GitHub for the latest official release (can be turned off in the settings). Nothing about you is sent.
- **Local files only:** settings and the host's autosaves are saved on the host's PC.

Everyone in a room needs the same Geometry Dash and NexLevel version.
