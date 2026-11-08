# Phase 1 test log (GD 2.2081, Geode 5.11.2 loader, Windows)

| Date | Test | Result | Evidence (Geode log) |
| --- | --- | --- | --- |
| 2026-11-08 | Mod loads | PASS | `NexLevel loaded: target GD 2.208x, Geode major 5, protocol v1` |
| 2026-11-08 | Network probe (Asio loopback TCP + framing) | PASS | `network probe OK ... round trip 22-32 us, 532 bytes` |
| 2026-11-08 | Editor: create objects | PASS | `created uid=... (via addToSection)` |
| 2026-11-08 | Editor: move (arrows, small and large steps) | PASS | `changed ... 2=<x>`, `3=<y>` |
| 2026-11-08 | Editor: rotate | PASS | `changed ... 6=90` |
| 2026-11-08 | Editor: duplicate | PASS | 2x `created` |
| 2026-11-08 | Editor: delete (trash) | PASS after fix 5c39155 | `removed uid=...` |
| 2026-11-08 | Editor: undo / redo of delete | PASS | `created` on undo, `removed` on redo |
| 2026-11-08 | Editor: move across many sections (x 315 -> 5105) | PASS | no false `removed` |
| 2026-11-08 | Editor: playtest | PASS | no diffs while playtesting |
| 2026-11-08 | Editor: colour change via Edit Object | PASS | `changed ... 21=1`, detected live while the popup is open (earlier runs: colour was never actually applied) |

| 2026-11-08 | Core sync engine (Linux, ASan/UBSan) | PASS | 107 checks: NetIds, op wire format incl. hostile input, replica rules, host sequencing/dedupe/forged ids, optimistic client view, 40 randomized 2-6 peer convergence runs |

| 2026-11-08 | Session layer over real TCP (Linux, ASan/UBSan + 5x TSan) | PASS | embedded host + 3 clients: initial sync of pre-existing objects, 536 concurrent ops on 92 objects converged; GD 2.209x client refused (WrongGameVersion); leave/stop detected; 123 checks, 0 data races |

| 2026-11-08 | Core tests on Windows (MSVC), incl. sync engine + TCP session | PASS | 123 checks, 0 failed |

| 2026-11-08 | First real room: GD (host, mod) + NexBot (same PC, 127.0.0.1) | PASS | bot built 5 blocks/deleted 1 while host was out of the editor; host re-entered: `editor rebuilt from room (5 objects)`; host edits published |

## Findings
- `GameObject::m_uniqueID` is not a stable identity: after undo/redo the same object was logged as uid 3941 when re-added and uid 3942 when removed again. Confirms the design decision to use NexLevel NetIDs.
- `EditorUI::deleteObject` and `LevelEditorLayer::undoLastAction/redoLastAction` are inline on Windows (also noted by ZyyeDev/MultiplayerEditor).
- Arrow moves generate one diff per press; continuous drags will need coalescing before sending over the network (per-tick batching).
