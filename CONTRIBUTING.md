# Contributing

Thanks for taking an interest. Bug reports, hardware ports, and documentation fixes are all welcome.

## Before you start

- For anything bigger than a small fix, open an issue first so we can agree on the approach.
- Security problems go through [SECURITY.md](SECURITY.md), not public issues.

## Development setup

- **Firmware**: follow [docs/tutorials/01-hub.md](docs/tutorials/01-hub.md) to install the ESP32 core and libraries.
  Every sketch must still compile: `hub`, `node-door`, `node-laser`.
- **Dashboard**: follow [docs/tutorials/05-dashboard.md](docs/tutorials/05-dashboard.md). `npm run build` must pass.

CI checks both on every pull request.

## Code style

Match the code around you:

- **Firmware**: one module per `namespace`, `// ========== [Section] ==========` markers,
  every tunable in `config.h` (hub) or the `#define` block at the top of a node sketch.
  Comments explain *why*, never *what*.
- **Dashboard**: plain JavaScript, no TypeScript, no UI framework. Every API route checks its
  auth (session or `x-api-key`) as the first thing it does.
- **Protocol**: changing `SecFrame` or the meaning of an event code is a protocol break.
  Bump `SECMESH_VERSION` and say so in the pull request.

## Pull requests

- Keep each PR to one change.
- Describe how you tested it, and on what hardware.
- Update the docs if behaviour, pins, or configuration changed.
- Add a line to [CHANGELOG.md](CHANGELOG.md) under *Unreleased*.
