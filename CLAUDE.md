# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository focus

This repository’s primary runnable target is `main_service`, a Linux-side C service for a Rockchip-based board that bridges:
- Tongqu AI agent SDK (cloud chat/audio/IoT descriptor registration)
- MCU control/status over Tuya UART protocol
- Local audio and local voice IPC modules (currently stubbed/integration-stage)

Most implementation work should be assumed to happen in `main_service/`.

## Build, run, deploy commands

### Main service build

From repo root:

```bash
cd main_service
./build.sh
```

Strip binary for smaller release artifact:

```bash
cd main_service
./build.sh --strip
```

### Deploy to target board

`deploy.sh` builds and SCPs `proai_service` to the configured board path:

```bash
cd main_service
./deploy.sh
./deploy.sh --strip
```

### Run service

From the board deployment directory (or wherever `proai_service` is placed):

```bash
./proai_service
./proai_service -s -v 0
./proai_service -h
```

CLI flags from `main_service/main.c`:
- `-s`: stdout logging only (no file log)
- `-v <0-4>`: `0=DEBUG,1=INFO,2=WARN,3=ERROR,4=NONE`
- `-h`: usage help

### MCU simulator for local integration

```bash
cd mcutools
python3 rabbit_mcu_sim.py
```

This creates `/tmp/ttyModule`, which `main_service` uses as default UART device.

## Tests and linting

There is no repository-level unit-test/lint framework configured (no `pytest`, `ctest`, `make test`, or dedicated lint config discovered in current tree). Validation is currently build-and-run/integration driven.

If you need a single-target verification loop, use:

```bash
cd main_service
./build.sh
```

and then run the produced binary against either real MCU or `mcutools/rabbit_mcu_sim.py`.

## Architecture overview (big picture)

### Runtime model

`main_service/main.c` defines a two-lane architecture:

1. **Control lane (queued, serialized):**
   - Producers (AI callbacks, Tuya callbacks, offline voice, timer tasks) enqueue `SystemMsg` into global `g_sys_queue`
   - Main loop dequeues and performs UART writes via a single write path (`tuya_send_cmd`)
   - This enforces ordered, single-writer command transmission to MCU

2. **Audio lane (bypass path):**
   - AI audio callback (`on_audio`) sends data directly to `audio_module_play`
   - Audio does not pass through control queue, preventing control-command starvation from streaming audio

### Concurrency and reconnection

- Main thread: queue consumer + periodic tasks
- `uart_rx_thread`: UART open/read/parse loop with auto-reconnect behavior
- Queue implementation (`src/queue.c`) uses mutex + condition variables with timed pop, acting as backpressure and producer/consumer decoupling

### Protocol and dispatch boundaries

- UART frame packing/parsing and MCU command constants are handled by Tuya protocol modules (`tuya_protocol.*`)
- Parsed MCU frames are dispatched through `tuya_mcu_dispatcher_t` callbacks in `main.c`
- Current upstream-from-MCU behavior is mostly logging/placeholder routing; downstream-to-MCU command path is the most complete path

### Cloud integration boundary

`src/cloud_llm.c` wraps Tongqu `agent_sdk` client lifecycle:
- client create/config
- auth/connection establishment (`agentEnsureAuthorizedConnection`)
- IoT descriptor registration (`agentSendJson`)
- text/json/audio send helpers
- status/error/message callbacks

The file also sets TLS-related environment vars (`AGENT_CA_BUNDLE`, `SSL_CERT_FILE`, `CURL_CA_BUNDLE`) and uses fixed cloud endpoints/device auth inputs from current code.

### Build/link dependencies that matter

`main_service/CMakeLists.txt` expects SDK artifacts in sibling paths:
- `../tongqu-sdk/agent_linux_sdk_rockchip830`
- `../tongqu-sdk/rockchip830_runtime_bundle/lib`

Binary links against `agent_sdk`, `websockets`, `curl`, `pthread`, `m`, `rt`, `dl`.

If runtime linking fails on target, verify `LD_LIBRARY_PATH` includes `rockchip830_runtime_bundle/lib`.

## Important docs to consult

- `main_service/README.md`: end-to-end service architecture, startup options, packaging/deploy notes
- `example/README.md`: standalone SDK voice example, env var contract, expected runtime logs

No Cursor rules or Copilot instruction files were found in this repository snapshot (`.cursorrules`, `.cursor/rules/`, `.github/copilot-instructions.md` absent).