# NexLevel v1 test plan (GD 2.2081)

Nothing counts as supported until it passes here. Use `tools\bot.ps1` when there is no second PC.

| # | Test | How | Pass when |
|---|---|---|---|
| 1 | Core | `tools\test-core.ps1` | `0 failed` |
| 2 | Build | `tools\build.ps1 -Target gd2208` | builds, GD loads `NexLevel loaded ... protocol v5` |
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
| 13 | Quit while hosting | Close GD with a room open | no crash |
| 14 | Real distance | A friend on another network (ideally mobile data) via relay | joins and edits |
