# SuperLexicon

SuperLexicon is the **Super-App** for the Lexicon blockchain ecosystem. Running on Linux, macOS, and the Canaan K230 (`568x1232` AMOLED slate), it provides a unified interface combining a real-time **Blockchain & Network Dashboard** with native on-chain applications, while supervising the local `lexicon-node` and `LR2021` FLRC radio bridge:

- **Blockchain & Network Dashboard**: Live network statistics, blockchain metrics (block height, finality latency, TPS, active validators, consensus phases `P1`-`P4`), RF link telemetry & subnet discovery (`JOIN` / `LEAVE`), and node role control (`VALIDATOR` / `ROAMER` / `REPEATER`).
- **LexiChat**: Censorship-resistant on-chain mesh messaging (`LCHT` transactions) across public and encrypted channels (`#general`, `#mesh-ops`, `#gov`).
- **LexiDEX**: Decentralized on-chain exchange and liquidity pools for peer-to-peer asset swaps over the radio mesh.
- **LexiWallet**: Self-custodial account vault (`secp256k1` / `Ed25519`), balance and nonce tracking, and transaction signing (`eth_sendRawTransaction`).
- **LexiApps**: On-chain `.wasm` application registry, chunked package downloader, and sandboxed Wasmtime execution environment.

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
