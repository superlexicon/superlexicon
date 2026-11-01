# SuperLexicon

SuperLexicon is the client application for the Lexicon blockchain protocol. It runs on Linux, macOS, and the Kendryte K230, managing the local `lexicon-node` and radio transport bridge while providing a unified interface for on-chain applications:

- **Lexicon**: Subnet discovery (`JOIN` / `LEAVE`), consensus status (`P1`-`P4`), recent blocks, and active validators.
- **LexiWallet**: Account vault, balance and nonce tracking, and transaction signing (`eth_sendRawTransaction`).
- **LexiChat**: On-chain mesh messaging (`LCHT` transactions) across channels (`#general`, `#mesh-ops`, `#gov`).
- **LexiApps**: On-chain `.wasm` application registry, package downloader, and Wasmtime execution view.

## Build

Requires a C11/C++20 compiler and SDL2 development headers (`libsdl2-dev` on Debian/Ubuntu, `sdl2` via Homebrew on macOS).

```bash
make -j$(nproc)
./bin/superlexicon
```

## Options

```text
--tab <lexicon|wallet|chat|apps>  Select initial view
--rpc-host <ip>                   JSON-RPC host (default: 127.0.0.1)
--rpc-port <port>                 JSON-RPC port (default: 8545)
--ws-port <port>                  WebSocket stats port (default: 8566)
--no-daemons                      Do not spawn lexicon-node or lexicon-radio-bridge
--scale <factor>                  Window scale factor
--headless [ticks]                Run offscreen for automated testing
--screenshot <path.bmp>           Dump 568x1232 framebuffer to BMP
```
