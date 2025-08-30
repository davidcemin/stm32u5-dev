# MX Wi‑Fi Host Driver: Architecture and Porting Guide

This document explains how the MX Wi‑Fi host driver (EMW3080B) is organized, how data flows through the stack, and how to port it to a different OS or platform. It’s written from reading and tracing the code under `mx_wifi_library/Drivers/BSP/Components/mx_wifi` and the surrounding `core/` and `io_pattern/` directories.

- Upstream source root in this workspace: `mx_wifi_library/Drivers/BSP/Components/mx_wifi/`
- Key support code: `mx_wifi_library/Drivers/BSP/Components/mx_wifi/core/` and `io_pattern/`
- Zephyr adaptation example: `drivers/wifi/emw3080_mx/mx_wifi_conf.h`


## 1) Big picture

The host driver is layered and message‑based:

```
Your app  ─────────────────────────────────────────────────────────────────────────────
   |            Public API: mx_wifi.h/.c (Wi‑Fi control, sockets, TLS, mDNS, FOTA)
   v
IPC (mipc): core/mx_wifi_ipc.h/.c  ← synchronizes requests/responses, dispatches events
   v
HCI: core/mx_wifi_hci.h/.c         ← wraps/unwraps frames, RX FIFO, UART SLIP for UART
   v
I/O transport: io_pattern/          ← SPI (mx_wifi_spi.c) or UART (mx_wifi_uart.c)
   v
Hardware module (EMW3080B): runs firmware implementing Wi‑Fi and (optionally) TCP/IP
```

Two operating modes:
- Offload mode (default in template): Host proxies socket and higher APIs to the module via IPC.
- Network bypass mode (raw L2): The module forwards Ethernet frames; the host stack (e.g., LwIP) processes them.

OS/HAL abstractions wrap threads, semaphores, buffers, time, and I/O to keep the core portable:
- Bare‑OS macros: `core/mx_wifi_bare_os.h`
- RTOS abstraction (CMSIS‑RTOS): `core/mx_rtos_abs.h/.c` (enabled with `MX_WIFI_USE_CMSIS_OS=1`)
- HAL shims for SPI/UART/GPIO are provided via `mx_wifi_conf.h` for your platform.


## 2) Source map (what lives where)

- Public API: `mx_wifi.h/.c`
  - Init/deinit, scan/connect/AP, link/IP info, power save
  - Socket proxy (create/bind/connect/send/recv/close, DNS, ping, getaddrinfo)
  - TLS, mDNS, FOTA, network‑bypass callbacks
- IPC core: `core/mx_wifi_ipc.h/.c`
  - Request/response queueing, `mipc_request`, `mipc_poll`
  - Async event table and dispatch (Wi‑Fi status, bypass input, FOTA)
- HCI: `core/mx_wifi_hci.h/.c`
  - TX: handoff to bus; UART uses SLIP framing (`core/mx_wifi_slip.h/.c`)
  - RX: frame queue; `mx_wifi_hci_input` enqueues from bus ISR/task
- I/O transport: `io_pattern/`
  - SPI: `mx_wifi_spi.c` (IRQ/FLOW handshake, half‑duplex transfers)
  - UART: `mx_wifi_uart.c` (ISR + circular buffer + SLIP decode)
- OS/Buffer abstractions:
  - Bare OS: `core/mx_wifi_bare_os.h` (locks/sems/threads/FIFOs, buffer API)
  - CMSIS‑RTOS: `core/mx_rtos_abs.h/.c`
- Address/Sockets helpers: `core/mx_address.h/.c`
- Configuration:
  - Template: `mx_wifi_conf_template.h`
  - Platform-specific (example Zephyr): `drivers/wifi/emw3080_mx/mx_wifi_conf.h`


## 3) Data and control flow

### 3.1 Initialization

1) Your code allocates an `MX_WIFIObject_t` and registers bus I/O:
   - `MX_WIFI_RegisterBusIO()` wires callbacks (init, deinit, send, receive, reset, delay, get_tick, etc.).
2) `MX_WIFI_Init()` sequence (simplified):
   - Calls IO init and resets the module
   - Initializes HCI and IPC (`mipc_init`) and starts RX service (thread or yield loop)
   - Queries FW version and MAC via `mipc_request` calls

### 3.2 Synchronous request/response

- Public APIs (e.g., `MX_WIFI_Connect`, socket ops) pack parameters into a payload and call `mipc_request(api_id, payload, timeout)`.
- IPC adds a header `[req_id(4) | api_id(2)]` in front of your payload (see `mx_wifi_conf_template.h`: `MIPC_HEADER_SIZE=6`).
- HCI sends the frame to the bus; bus driver transmits it to the module.
- The RX loop dequeues frames from HCI, matches `req_id`, and signals the waiting thread with the response payload.

### 3.3 Asynchronous events

- Module‑initiated notifications (Wi‑Fi status change, bypass RX, FOTA progress) are delivered without a pending request.
- IPC looks up a callback in its event table and invokes the registered handler in your `MX_WIFIObject_t` (if set).

### 3.4 UART vs SPI specifics

- UART uses SLIP (Serial Line IP) framing. `core/mx_wifi_slip.c` encodes/decodes frames; UART ISR feeds bytes into a circular buffer and a worker reconstructs frames and calls `mx_wifi_hci_input()`.
- SPI uses a simple header and handshakes (IRQ/FLOW/CS). A dedicated task runs a TX/RX loop, pushing received payloads to HCI.


## 4) Key configuration knobs

From `mx_wifi_conf_template.h` (or your platform’s `mx_wifi_conf.h`):

- Transport choice: `MX_WIFI_USE_SPI` (1 for SPI, 0 for UART)
- RTOS vs bare metal: `MX_WIFI_USE_CMSIS_OS` (1 to use CMSIS abstraction)
- Network bypass: `MX_WIFI_NETWORK_BYPASS_MODE` (1 enables raw L2 mode)
- Buffer sizing:
  - `MX_WIFI_MTU_SIZE` (typically 1500)
  - `MX_WIFI_BUFFER_SIZE` depends on bypass or offload mode
  - `MX_WIFI_IPC_PAYLOAD_SIZE = MX_WIFI_BUFFER_SIZE - 6`
  - `MX_WIFI_SOCKET_DATA_SIZE = MX_WIFI_IPC_PAYLOAD_SIZE - 12`
- Timeouts and threading: `MX_WIFI_CMD_TIMEOUT`, thread priorities/stack sizes
- Queues/backpressure: `MX_WIFI_MAX_RX_BUFFER_COUNT`, `MX_WIFI_MAX_TX_BUFFER_COUNT`
- UART specifics: `MX_CIRCULAR_UART_RX_BUFFER_SIZE`, `MX_WIFI_UART_BAUDRATE`
- TX no‑copy: `MX_WIFI_TX_BUFFER_NO_COPY` (1 = prefer zero‑copy from IP stack)

Platform headers (e.g., Zephyr) also remap memory allocators and HAL symbols to your OS/HAL.


## 5) Contracts at each layer

- Public API (mx_wifi)
  - Input: typed structs and params; timeouts are in ms; returns `MX_WIFI_STATUS_t` or positive sizes.
  - Errors: timeouts, busy, invalid params; propagate module error codes via IPC payloads.
- IPC (mipc)
  - Packets: 6‑byte header + payload; `req_id` matches responses; reentrancy protected via locks.
  - Errors: request timeout; RX queue full.
- HCI
  - RX FIFO of frames; producer = bus driver; consumer = IPC.
  - Errors: out‑of‑buffers; invalid frames; SLIP decode errors (UART).
- I/O transport
  - SPI: requires IRQ/FLOW GPIOs; half‑duplex; transfer scheduling thread; uses semaphores/locks.
  - UART: ISR byte pumping; SLIP framing; circular buffer sizing affects latency.
- OS/Buffer abstraction
  - Must provide: locks, semaphores, threads, FIFO, timing (`HAL_GetTick`, `HAL_Delay`), memory alloc/free, and net buffer helpers.


## 6) Typical sequences (examples)

### 6.1 Connect to an AP (offload mode)

1) `MX_WIFI_Init()` completes; module FW version and MAC read.
2) `MX_WIFI_Connect(ssid, pass, sec)` → `mipc_request(CONNECT, params)`
3) Module joins AP; async status event indicates association; DHCP may run on module.
4) `MX_WIFI_GetIPAddress()` to read IP info (or subscribe to link events).

### 6.2 Socket send/recv (offload mode)

1) `MX_WIFI_Socket_create(AF_INET, SOCK_STREAM, 0)` → remote fd on module
2) `MX_WIFI_Socket_connect(fd, sockaddr)`
3) `MX_WIFI_Socket_send(fd, buf, len)`/`recv` loop; payload chunking must respect `MX_WIFI_SOCKET_DATA_SIZE`.

### 6.3 Network bypass RX/TX

- Register bypass callbacks using `MX_WIFI_Network_bypass_mode_set()`.
- RX: module pushes `[MIPC_HDR | bypass_rparams | L2 frame]` → IPC dispatches to your bypass input callback; you pass the L2 frame into your stack (e.g., LwIP input).
- TX: your stack provides a pbuf/mbuf with headroom ≥ `MX_WIFI_MIN_TX_HEADER_SIZE`; driver transmits without extra copy when `MX_WIFI_TX_BUFFER_NO_COPY=1`.


## 7) Porting to a new OS/platform

Porting is largely “wiring” the OS/HAL abstractions and choosing UART vs SPI.

Checklist:

1) Create `mx_wifi_conf.h` for your platform (use the template as a base):
   - Set `MX_WIFI_USE_SPI` or UART; decide `MX_WIFI_NETWORK_BYPASS_MODE`.
   - Tune buffer sizes and timeouts; map memory allocators (e.g., to your RTOS heap).
   - Provide or map `HAL_GetTick`, `HAL_Delay` to your system clock/timer.

2) HAL shims for I/O and GPIO (referenced by SPI/UART drivers):
   - SPI: define `SPI_HandleTypeDef` surrogate and implement `HAL_SPI_Transmit`, `HAL_SPI_Receive`, `HAL_SPI_TransmitReceive` (+ DMA variants if desired), CS control, and GPIO reads for IRQ/FLOW.
   - UART: define `UART_HandleTypeDef` surrogate and implement `HAL_UART_Receive_IT` (or equivalent), ISR hook to deliver bytes, and transmit functions.
   - GPIO: macros/helpers for pin read/write, IRQ configuration; connect module’s IRQ/FLOW lines.

3) OS primitives (if not using CMSIS‑RTOS):
   - Implement or map macros in `mx_wifi_bare_os.h` to your OS: locks, semaphores, threads, FIFOs, and net buffer alloc/free.
   - Alternatively enable `MX_WIFI_USE_CMSIS_OS=1` and provide CMSIS‑RTOS v2.

4) Integrate the bus driver:
   - For SPI, adapt `io_pattern/mx_wifi_spi.c` to your HAL (or use it as a reference). Ensure the TX/RX task and IRQ handlers are wired.
   - For UART, adapt `io_pattern/mx_wifi_uart.c` with your UART ISR and SLIP handling.
   - Expose the probe/attach function (e.g., `mxwifi_probe`) that fills an `MX_WIFI_IO_t` and calls `MX_WIFI_RegisterBusIO()`.

5) Threading model:
   - Ensure the RX service runs: either created by the driver (thread) or polled via `MX_WIFI_IO_YIELD()` in a periodic task.
   - Respect the thread priorities and stacks in your OS; increase if you see drops.

6) Test plan:
   - Smoke: init/reset, get FW version/MAC.
   - Wi‑Fi: scan, connect, DHCP/IP retrieval.
   - Sockets: TCP connect/send/recv; UDP send/recv; DNS and ping.
   - Stress: large transfers near `MX_WIFI_SOCKET_DATA_SIZE`; concurrent sockets.
   - Bypass mode: L2 RX/TX into your stack; verify headroom and no‑copy behavior.

Common pitfalls:
- Buffer sizing too small → IPC truncation; verify `MX_WIFI_IPC_PAYLOAD_SIZE` and `MX_WIFI_SOCKET_DATA_SIZE`.
- Missing headroom in bypass TX when no‑copy is enabled → frames dropped. Ensure `>= MX_WIFI_MIN_TX_HEADER_SIZE` headroom.
- ISR vs thread priority inversion → RX backlog. Tune priorities and queue sizes.
- HAL timing functions not monotonic or wrong units → timeouts.


## 8) Limits and sizing quick reference

- IPC header: 6 bytes → `MX_WIFI_IPC_PAYLOAD_SIZE = MX_WIFI_BUFFER_SIZE - 6`
- Socket data chunk: `MX_WIFI_SOCKET_DATA_SIZE = MX_WIFI_IPC_PAYLOAD_SIZE - 12`
- MTU typical: 1500 bytes; choose `MX_WIFI_BUFFER_SIZE` accordingly (see template)
- Queues: `MX_WIFI_MAX_RX_BUFFER_COUNT`, `MX_WIFI_MAX_TX_BUFFER_COUNT` help prevent stalls at the cost of memory


## 9) Minimal usage sketch

```c
MX_WIFIObject_t wifi_obj = {0};
MX_WIFI_IO_t io = {0};

// Fill io.* with your bus functions (init, deinit, reset, send, recv, delay, get_tick)
MX_WIFI_RegisterBusIO(&wifi_obj, &io);

if (MX_WIFI_Init(&wifi_obj) == MX_WIFI_STATUS_OK) {
  MX_WIFI_Connect(&wifi_obj, (const uint8_t*)"ssid", (const uint8_t*)"pass", MX_WIFI_SEC_WPA2_WPA3);
  // ... sockets or bypass usage here ...
}
```

In a Zephyr‑style port, see `drivers/wifi/emw3080_mx/mx_wifi_conf.h` for concrete HAL mappings and config values used in this workspace (SPI + bypass + bare‑OS macros mapped to Zephyr APIs).


## 10) File pointers (non‑exhaustive)

- `.../mx_wifi/` top API: `mx_wifi.h`, `mx_wifi.c`
- IPC: `core/mx_wifi_ipc.h`, `core/mx_wifi_ipc.c`
- HCI: `core/mx_wifi_hci.h`, `core/mx_wifi_hci.c`
- SLIP: `core/mx_wifi_slip.h`, `core/mx_wifi_slip.c`
- SPI: `io_pattern/mx_wifi_spi.c`
- UART: `io_pattern/mx_wifi_uart.c`
- OS abs: `core/mx_wifi_bare_os.h`, `core/mx_rtos_abs.h/.c`
- Addr utils: `core/mx_address.h`, `core/mx_address.c`
- Config: `mx_wifi_conf_template.h` and your platform’s `mx_wifi_conf.h`


## 11) Appendix: quick port recipe

- Decide SPI or UART; wire pins and IRQs.
- Copy the conf template; set transport, mode, and sizes; map HAL and allocators.
- Bring up RX thread/service; verify `MX_WIFI_Echo()` and FW version.
- Implement minimal Wi‑Fi join; then sockets or bypass path depending on your mode.
- Add logging (define `MX_WIFI_*_DEBUG`) during bring‑up; turn off afterward.

---

If you need a concrete skeleton for your OS/HAL shims, duplicate the Zephyr config (`drivers/wifi/emw3080_mx/mx_wifi_conf.h`) and adapt the HAL mapping functions and macros to your environment.