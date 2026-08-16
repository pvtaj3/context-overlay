# Contributing to Context Overlay

Two-agent workflow (branch-per-feature + pull requests). This repo is
developed by two collaborating agents; `main` is protected and all changes
land via reviewed PRs.

## Branch strategy
- `main` is protected. **Never push directly to `main`.**
- Branch off the latest `main` using a typed prefix:
  - `feature/<slug>` — new capability (e.g. `feature/dwell-telemetry`)
  - `fix/<slug>`     — bug fix
  - `chore/<slug>`   — build / tooling
  - `docs/<slug>`    — documentation
- Keep branches small and atomic: one logical change per PR.
- Before opening a PR, sync your branch with `main`
  (`git fetch origin && git rebase origin/main`) to avoid conflicts.

## Opening a pull request
- Target branch is always `main`.
- Fill the PR template: link the Notion milestone/phase, describe the change,
  and state build + Windows 11 behavioural-check status.
- At least **one approving review from the other agent** is required before merge.
- All review conversations must be resolved before merge.
- Force-pushes to `main` are blocked; `main` cannot be deleted.

## Reviews
- The two agents review each other's work. GitHub requires the approval to come
  from a reviewer who is **not the PR author**.
- When CI is enabled, it must be green before merge.

## Build
- Target: Windows 11, C++20, CMake, Win32 API.
- Visual Studio 2022 (MSVC):
  `cmake -S . -B build -G "Visual Studio 17 2022" && cmake --build build`
- Cross-compile sanity gate (Linux/WSL, no Windows SDK):
  MinGW-w64 — verifies the Win32 API usage is structurally correct and linkable.
- Behavioural correctness (click-through, no focus theft, DPI) must be confirmed
  on real Windows 11 against the named targets: Chrome, Terminal, Rhino 3D.
