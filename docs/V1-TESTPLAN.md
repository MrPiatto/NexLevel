# NexLevel v1 test plan (GD 2.2081)

Nothing counts as supported until it passes here. Use `tools\bot.ps1` when there is no second PC.

| # | Test | How | Pass when |
|---|---|---|---|
| 1 | Core | `tools\test-core.ps1` | `0 failed` |
| 2 | Build | `tools\build.ps1 -Target gd2208` | builds, GD loads `NexLevel loaded ... protocol v8` |
| 3 | Host + join | Host room, Copy, `tools\bot.ps1 join` | bot's 5 blocks appear; move, rotate, colour, delete arrive; no crash |
| 4 | Host edits reach players | Place/move/delete while the bot is in the room | bot prints each change |
| 5 | Level settings | Change a colour channel, background, ground, song | a second player sees the same; the same after leaving and rejoining |
| 6 | Presence | Two players in the room | each sees the other's coloured frame and name |
| 7 | Big level | Host a level with 10k+ objects, join | join finishes in seconds |
| 8 | Host leaves editor | Host exits to the menu | players keep building; host re-enters and sees their work |
| 9 | Reconnect | Disable Wi-Fi on a player for ~10 s | "Reconnected", nothing lost |
| 10 | Kick / close | Kick the bot; Close room | bot is told and cannot rejoin; everyone is told |
| 11 | Relay | `tools\bot.ps1 relay`, setting = 127.0.0.1, Host, bot join with `NEXLEVEL_RELAY` | 10-character code works |
| 12 | Undo / redo / copy-paste | Do each with a player in the room | the player ends up with the same level |
| 13 | Quit while hosting | Main menu Quit (or Esc) with a room open | "Stop hosting?" warning; Cancel keeps the room; Close GD saves and closes, no crash. Also close the window with X: no crash, level saved |
| 18 | Hosting badge | Host, exit the editor, go to the menus / level list | "Hosting NexLevel room - N players" at the bottom of every screen; gone in the editor and after Close room |
| 19 | Autosave (host) | Host exits the editor, bot builds for 2 min, then kill GD from Task Manager and reopen | the bot's objects are in the host's level (autosave every 90 s, log `autosaved`) |
| 20 | No copy for players | Join (from the editor and from the menu), build, try Save, then leave | Save says "Only the host saves this level"; the player's own level is unchanged; the temporary "NL - <host>" level disappears after leaving; pasting in another level pastes nothing |
| 15 | Ping / Info | Collab menu with the bot in the room; press Info | ping shows in ms; graph moves; IP hidden until Show; profile button opens the GD profile |
| 16 | Watch playtests | Player A playtests while B stays in the editor | B sees A's icon move (right mode: ship, ball...), a trail in A's colour that stays after A stops; a new test starts a new trail |
| 17 | Everything syncs | Use undo/redo, copy/paste, duplicate, align, edit group, trigger settings, text objects, keyframes, select-all + move | the other player ends up with an identical level (also check after 10 s for sweep corrections in the log) |
| 21 | Host on a copy | Host room -> "A copy", build with the bot, Save and Exit | a level "<name> (NL)" has everything; the original level is unchanged. "This level" edits the original. Esc on the question opens no room |
| 22 | Main menu button | Tap the logo at the top right of the main menu, with and without a room open | menu opens; Host card explains to use the editor; Join with a code opens the editor on a new level "NL - <host>" with the room, other levels untouched; with a room open shows code, Copy, Close room; button not overlapping anything |
| 14 | Real distance | A friend on another network (ideally mobile data) via relay | joins and edits |
