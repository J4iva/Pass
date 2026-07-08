<div align="center">
  <br>
  <img src="https://img.shields.io/badge/C%2B%2B-20-blue?style=flat-square&logo=cplusplus" alt="C++20">
  <img src="https://img.shields.io/badge/Qt-6-green?style=flat-square&logo=qt" alt="Qt 6">
  <img src="https://img.shields.io/badge/license-GPLv3-blue?style=flat-square" alt="GPLv3">
  <img src="https://img.shields.io/badge/build-CMake%2FNinja-important?style=flat-square" alt="CMake + Ninja">
  <h3>🏁 Pass</h3>
  <p><strong>Desktop dashboard for students — calendar, notes, study sessions & multi-device sync</strong></p>
</div>

---

**Pass** is a native Windows desktop application that integrates everything a student needs in one place:

- **📅 Calendar** with bidirectional **Google Calendar** sync (OAuth 2.0 + PKCE)
- **📝 Notes** as Markdown files compatible with **Obsidian**
- **⏱ Study sessions** with configurable pomodoro strategies (Pomodoro, 52/17, custom) and per-subject statistics
- **🔄 Multi-device sync** via a private **GitHub** repository (last-writer-wins, tombstone-based CRDT)
- **📋 CLI-by-command** — create subjects, topics, notes, events, tasks, and sessions by writing plain-text commands to the sync repo, without opening the app
- **🏷 Tasks** as first-class calendar events (`[T]` convention) with per-subject tracking

## Tech stack

| Layer | Technology |
|-------|-----------|
| Language | C++20 |
| UI | Qt 6 Widgets + Qt Charts |
| Persistence | SQLite via Qt SQL (WAL mode) |
| Cloud sync | Google Calendar API v3 — OAuth 2.0 + PKCE (Qt NetworkAuth) |
| Secret storage | Windows Credential Manager (wincred / advapi32) |
| Cross-device sync | System `git` via `QProcess` + GitHub private repo |
| Build | CMake ≥ 3.25 + Ninja (MSYS2 UCRT64, GCC) |
| License | [GPLv3](LICENSE) |

## Screenshots

<!-- Consider adding screenshots here -->

## Features

### Dashboard
Overview of upcoming events, active sessions, and quick stats.

### Calendar
- Month/week/day views with event management
- Full bidirectional sync with Google Calendar (write-through with etag-based conflict detection)
- Incremental sync via `syncToken` (full resync on 410)
- Task support (`[T]`-prefixed events with subject assignments)

### Notes (Obsidian vault)
- Notes stored as `.md` files with YAML frontmatter
- Compatible with Obsidian — the filesystem is the source of truth
- Subject/topic tagging in frontmatter
- Vault watcher that triggers cross-device sync on file changes

### Study sessions
- Configurable time strategies: Pomodoro (25/5), 52/17, custom
- Per-subject accumulated time statistics
- Planned vs. running sessions with resume support on app restart
- Visual timer with phase indicators

### Multi-device sync
- Data mirrored as individual JSON files (`data/`) in a private GitHub repo
- Tombstone-based deletion tracking (absence ≠ deletion)
- Deterministic UUIDv5 for strategy IDs (consistent across devices)
- Git sync runs in a **worker thread** — UI never freezes
- Second SQLite connection (WAL mode + `busy_timeout`) for concurrent read/write
- Notes synced via git mirror of the `notes/` subfolder

### CLI by command
Write plain-text commands to the sync repo (`command/*.passcmd`) from any device:
```
Pass create subject Cálculo --color "#3478f6"
Pass create topic Integrales --subject Cálculo
Pass create note "Repaso regla de la cadena" --subject Cálculo --body "..."
Pass create event Examen --start 2026-06-20T08:00:00Z --subject Cálculo
Pass create task "Práctica 3" --due 2026-06-22T22:00:00Z --subject Cálculo
Pass create session --start 2026-06-14T16:00:00Z --minutes 50 --subject Cálculo
```
Idempotent by design (UUIDv5 derived from command text), 8 KiB max, 200 per cycle, parser whitelist — no shell, no `eval`.

### Security
- OAuth 2.0 + PKCE S256 with loopback redirect on ephemeral port
- Tokens and client secrets stored exclusively in Windows Credential Manager
- Git credentials handled by Git Credential Manager (the app never sees them)
- Whitelist-based remote URL validation for git operations
- `GIT_TERMINAL_PROMPT=0`, timeouts with forced kill, redacted log output
- Binary hardening: `-fstack-protector-strong`, ASLR/DEP, `_FORTIFY_SOURCE=2`
- Command parser: strict whitelist (action/entity/flags) — no dynamic execution

## Quick start

### Prerequisites

- Windows (MSYS2 UCRT64 environment)
- [MSYS2](https://www.msys2.org/) with GCC, CMake, Ninja, Qt 6
- Git with Git Credential Manager
- An Obsidian vault (optional)

### Build & run

```bash
cmake --preset debug
cmake --build --preset debug
./build/debug/app/pass.exe
```

### Run tests

```bash
ctest --preset debug
```

### Deploy

```powershell
.\scripts\deploy.ps1
```

Output goes to `dist/` — a self-contained folder with all Qt DLLs and MinGW runtime.

## Integration guides

- [Google Calendar setup](docs/google-calendar.md) — create your OAuth Client (ES)
- [GitHub sync setup](docs/github-sync.md) — multi-device sync configuration (ES)
- [Remote CLI commands](docs/commands.md) — how to write `.passcmd` files
- [PassPort integration](docs/passport-integration.md) — mobile companion spec

## Project structure

```
Pass/
├── CMakeLists.txt           # Root build: core + app + tests
├── core/                    # Static lib "passcore" — business logic (no Widgets)
│   ├── include/pass/        # Public headers
│   └── src/                 # Implementation
├── app/                     # Executable — Qt Widgets UI
│   ├── views/               # Page views (Dashboard, Calendar, Notes, etc.)
│   ├── widgets/             # Reusable widgets (Timer, Dialogs)
│   └── util/                # Shared UI helpers
├── tests/                   # 20+ test suites (QtTest + ctest)
├── docs/                    # Documentation & integration guides
└── scripts/                 # Build & deploy scripts
```

See [estructura.md](estructura.md) for a detailed breakdown (ES).

## License

[GNU General Public License v3.0](LICENSE)
