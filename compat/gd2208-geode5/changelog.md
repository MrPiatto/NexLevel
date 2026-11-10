# v1.0.6
- **Public rooms:** the host can list the room (Settings tab), and everyone sees the open rooms in the main-menu NexLevel menu and joins with one tap.
- **Credits:** each player's object count in the Players tab, and "Copy credits" for your level description.
- **Look here:** a button that shows everyone a ring where you are looking.
- **Follow:** the camera follows another player until you touch the editor.
- **Manage players (host):** view only, undo all their work, kick or ban (banned players can't come back).
- **Room settings (host):** max players and "Lock level settings" (only the host can change colours, song, background...).
- Fixed other players' view outlines and names piling up on screen (old frames that never went away).
- Fixed objects placed by other players landing on a different editor layer: they now keep the layer their author put them on.
- Colours of objects placed by other players are refreshed right away.
- Fixed: opening another level's editor while hosting sent that level's objects into the room when you came back. Only the room's own level is ever linked to the room now.
- Everyone in a room needs v1.0.6 (protocol v9). Update the relay server for public rooms.

# v1.0.5
- Main-menu NexLevel menu shows live community numbers: players online and rooms open on the official relays, and total downloads.
- Update notice: at startup NexLevel checks the official releases; if a newer version is out it shows what's new and installs it with one button, then restarts the game (3... 2... 1...).
- Welcome screen with the changelog the first time a new version starts.
- Everything is saved into the host's level itself: no more restore-point copies ("CP 18:40") or extra backup levels in My Levels.
- The room stays the host's: players never get a saved copy. Saving is disabled in a player's editor while it shows someone's room (their own level stays as it was), the level used to join from the main menu is deleted when they leave, and the editor clipboard is cleared when a room ends.
- Works with rooms of v1.0.3 and v1.0.4 (same protocol). Update the relay server to see live players and rooms.

# v1.0.4
- New mod page: how to host and join, all features, and a "Privacy and online connections" section that explains exactly what other players and the relay can see (no data is collected).
- Node IDs (with the mod prefix) for the hosting badge, the editor ping label and the players overlay (better compatibility with other mods).
- Works with rooms of v1.0.3 (same protocol).

# v1.0.3
- Much faster joins on big levels: the level is sent compressed (about 7 times less data), so joining through the relay or on mobile data takes a fraction of the time.
- Joining from the main menu now waits until the whole level has arrived, then opens it with Geometry Dash's own level loader (the fastest way to load a level).
- The host going back into the editor no longer rebuilds the level: it opens the saved room directly and only adds or removes what changed meanwhile.
- Joining from the editor keeps every object that is already identical instead of deleting and re-creating the whole level.
- Teleport portals: the exit portal is now handled together with its entry portal. Before, syncing it on its own could crash, and made GD create extra exit portals that piled up in the level.
- Fixed a crash when the host went back into the editor while the room was open.
- Huge levels autosave every 5 minutes instead of 90 seconds (each save of a very big level takes a moment).
- Network tab for the host: the gauge and the ping chart now show the host's own connection (round trip to the relay) instead of the slowest player's. Each player's ping stays in the Players tab.
- Room chat: new Chat tab; messages from others also pop up as notices.
- "Go" button in the Players tab: jumps your editor camera to where that player is working.
- View-only players: the host can turn editing off (and back on) for anyone; their edits are undone and they can still watch.
- Restore points: the host can save a copy of the room as its own level at any time, and one is saved automatically every 10 minutes while the room changes.
- "Placed by": select one object to see who placed it, in the editor label.
- If Geometry Dash closes unexpectedly during a room, NexLevel offers to open the logs folder and the bug report page at the next start.
- Safety: joining from the main menu never switches scene from inside a level; chat notices use plain characters only; automatic restore points are skipped on huge levels (use the button).
- Everyone in a room needs v1.0.3 (protocol v8).

# v1.0.2
- Fixed a crash (heap corruption) when another player's change arrived while you were building, especially with Shift held (continuous building), swiping or dragging. Their changes now apply as soon as you let go, and what you build during the gesture is sent in one go when you release.
- While any editor popup is open (trigger settings, Edit Object, colours, group IDs...), other players' changes that would replace or delete objects wait until it closes, and your edits from the popup are sent when you close it.
- Hardening against crashes: NexLevel keeps its own reference to every object it tracks (no stale pointers, whatever way GD deletes objects), skips objects GD cannot build, and only shows game modes and icons that exist in your game when watching playtests.
- Objects in a linked group are taken out of the group properly when another player deletes or changes them.
- Thinner outline for other players' view.

# v1.0.1
- Fixed a crash when another player deleted an object you had selected.
- Other players' view is now shown as an outline only (no tint over the level).

# v1.0.0 - first release
- Real-time collaborative editing: objects, level settings (colours, background, ground, song, start options) and live presence.
- Rooms with short codes, up to 16 players, kick, close room, room stays open when the host leaves the editor.
- Automatic reconnection, fast join of very large levels.
- Watch others playtest live in your editor, with a trail in each player's colour.
- Ping per player, ping in the editor, Info screen (connection, IP, port, internet quality, ping graph), GD profile buttons.
- NexLevel button (logo) at the top right of the main menu: opens the NexLevel menu. Join works from there (the room opens in a new level "NL - <host>", your levels stay untouched); hosting is done from the level's editor.
- Host chooses: host the open level itself, or a copy "<name> (NL)" so the original stays untouched.
- Autosave: the host's level is saved every 90 s while the room changes (also while the host is in the menus), when leaving the editor, when the room closes and when GD saves. Players get a backup copy "NL - <host>".
- "Hosting NexLevel room" badge on every screen outside the editor; warning before closing GD while hosting.
- Encrypted sessions; relay server support (any network, including mobile data); automatic router port opening (UPnP).
