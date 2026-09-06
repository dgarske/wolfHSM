# wolfHSM Intel IPSE Port

Support for running wolfHSM against the Intel Partner Security Engine
(IPSE), the isolated security engine on Intel Panther Lake (Core Ultra
Series 3) and later SoCs that hosts third-party partner firmware. The
intended deployment is the wolfHSM **server inside the IPSE** as wolfSSL
partner firmware (an on-die HSM) and the wolfHSM **client on the host**,
offloading wolfCrypt through the standard crypto callback (`WH_DEV_ID`).

The host reaches the engine over HECI: the Linux `/dev/mei*` character
device, a message-based channel where each read/write carries one whole
message bounded by a per-client MTU (commonly 512-4096 bytes).

## Layout

- `mei/` - the transport
  - `intel_mei_transport.c/.h` - fragmentation core. wolfHSM's comm layer
    requires packet-preserving transports up to `WH_COMM_MTU`, which is
    larger than a HECI message, so this transport splits packets into
    device messages (4-byte fragment header: tag, seq/final, offset) and
    reassembles them, over a small device-ops vtable (`intelMeiDevOps`).
    Implements both the client and server transport callback sets
    (`IMT_CLIENT_CB` / `IMT_SERVER_CB`).
  - `intel_mei_linux.c/.h` - host backend for `/dev/mei*`: connect by
    client UUID (`IOCTL_MEI_CONNECT_CLIENT`), MTU from the ioctl reply,
    nonblocking read/write. The MEI uapi is defined locally so old
    distributions build. The client UUID defaults to a provisional
    wolfSSL value until Intel assigns one.
  - `intel_mei_sim.c/.h` - simulator backend so development and CI need no
    IPSE hardware: AF_UNIX sockets preserving message boundaries with a
    configurable per-message limit (default 512B, forcing real
    fragmentation), NOTREADY/fault injection, in-process pair mode for
    tests and a named-socket mode for cross-process demos.
- `ipse/` - the engine side
  - `ipse_server.c/.h` - the firmware main loop: a complete wolfHSM server
    (RAM-simulated flash NVM, server-side wolfCrypt, MEI transport) over
    any device backend, serving reconnecting clients until stopped.
    Compiles and runs on POSIX today; targets the engine once the FDK is
    available.
  - `ipse_mailbox_devops.c/.h` - stub for the engine-side HECI mailbox,
    BLOCKED ON the Intel FDK. Bringing up real firmware means
    implementing these five functions plus swapping the ramsim NVM for
    the IPSE storage service and wiring the engine TRNG.

## Tests

`test/wh_test_transport_mei.c` (part of `whTest_Unit`, so it runs in every
`cd test && make && make run`): fragmentation sweeps at 512/1024/4096-byte
message limits, device backpressure, desync/corruption faults, and the
full wolfHSM client/server, crypto, and keywrap test batteries over the
simulator at a 512-byte MTU. The transport structs are covered by
`wh_test_check_struct_padding.c` (`-Wpadded`, 32- and 64-bit).

## Demo

`examples/posix/wh_ipse_demo/` runs the engine server skeleton as its own
process over the simulator's named socket and a host process performing a
TLS 1.3 handshake with client-side crypto offloaded to it:

```
cd examples/posix/wh_ipse_demo
make WOLFSSL_DIR=/path/to/wolfssl run
```

## Status

Pre-silicon: everything here runs against the simulator. Waiting on Intel
for the SIMX platform models, the IPSE FDK (toolchain, mailbox API,
storage/TRNG services), an assigned HECI client UUID, and hardware.
FDK-specific engine code is expected to land following the wolfHSM vendor
port pattern.
