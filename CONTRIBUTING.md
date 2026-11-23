# Contributing to Avalon

Avalon is a small, experimental fork of Eden for Switch 2 homebrew. Bug reports, test
results and patches are welcome through [GitHub](https://github.com/fpmorello/avalon).
Do not send Avalon problems to the Eden project.

## Reporting a problem

Open an issue with:

- the build id (`b<N>`) from the options screen header;
- the game, its version, and its container (NSP, NSZ, XCI or XCZ);
- the options you changed, if any;
- what you saw, and the latest launch log from
  `sdmc:/switch/avalon/.local/share/avalon/log/games/`.

Never attach keys, firmware, game files or saves, and never ask for them.

## Code

- Avalon is GPLv3 or later. By contributing, you agree to license your changes under
  the same terms. Keep the existing copyright headers.
- Follow the style of the surrounding code ([docs/policies/Coding.md](docs/policies/Coding.md)).
- Keep Switch 2 changes in `src/eden_horizon` and `src/common/horizon`, or behind
  `__SWITCH__` / `HORIZON_HOST` guards, so the shared core stays close to Eden.
- Add a host test in `tools/switch2/tests` when the change can run off-console, and
  note in the pull request what you tested on hardware.
- Change one thing per performance experiment, and include before/after logs from the
  same scene (see [docs/Switch2Performance.md](docs/Switch2Performance.md)).
- AI assistance is fine, but you are responsible for understanding and testing what
  you submit.
