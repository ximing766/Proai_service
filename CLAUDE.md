# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Repository focus

This repository’s primary runnable target is `main_service`, a Linux-side C service for a Rockchip-based board that bridges:
- Tongqu AI agent SDK (cloud chat/audio/IoT descriptor registration)
- MCU control/status over Tuya UART protocol
- Local audio and local voice IPC modules (currently stubbed/integration-stage)

Most implementation work should be assumed to happen in `main_service/`.


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

No Cursor rules or Copilot instruction files were found in this repository snapshot (`.cursorrules`, `.cursor/rules/`, `.github/copilot-instructions.md` absent).

## Important rules
- don't build or deploy main_service
- only modify files in main_service/
- don't create git commits in .claude/