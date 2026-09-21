# Virtual Device Driver & Binary Protocol Engine

A robust systems programming project in C implementing a **Virtual Peripheral Device Driver**, **Resilient Streaming Protocol Parser**, and **Hardware Register Emulator**.

---

##  Architecture Layout

```
09-virtual-device-driver/
├── Makefile                # Build with ASan & UBSan
├── README.md               # Documentation & protocol specification
├── include/
│   ├── protocol.h          # Binary frame definitions, opcodes, error codes
│   ├── crc16.h             # CRC-16-CCITT engine interface
│   ├── parser.h            # Streaming state machine parser (Phase 2)
│   ├── vdev.h              # Virtual hardware registers & emulator (Phase 3)
│   └── driver.h            # Host device driver interface (Phase 4)
├── src/
│   ├── crc16.c             # Fast 256-entry table-driven CRC-16-CCITT
│   ├── protocol.c          # Safe Big-Endian wire serialization & validation
│   ├── parser.c            # Ring buffer & defragmentation state machine (Phase 2)
│   ├── vdev.c              # Virtual peripheral hardware simulation (Phase 3)
│   └── driver.c            # Host driver request/response engine (Phase 4)
└── tests/
    ├── test_protocol.c     # Unit tests for CRC16, frame packing/unpacking
    └── test_streaming.c    # Fragmentation & stream stress tests (Phase 2)
```

---

##  Wire Frame Specification

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          MAGIC (0xAA55)       |    SEQ NUM    |  OPCODE/CMD   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|       PAYLOAD LENGTH (16-bit) |      PAYLOAD BYTES (...)      |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|          PAYLOAD BYTES (...)                  |    CRC-16     |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

* **Sync Word (`0xAA55`)**: Identifies frame boundary in raw streams.
* **Sequence Number (`uint8_t`)**: Detects missing or duplicate packets.
* **Opcode (`uint8_t`)**: Message command / response type.
* **Payload Length (`uint16_t`, Big-Endian)**: Length of variable payload ($0 \le N \le 256$).
* **Payload (`N` bytes)**: Command-specific arguments or telemetry data.
* **CRC-16 (`uint16_t`, Big-Endian)**: CRC-16-CCITT ($x^{16} + x^{12} + x^5 + 1$, polynomial `0x1021`) protecting Header + Payload.

---

## 🧪 Build & Test

```bash
make test
```
All targets compile with `-Wall -Wextra -Werror -pedantic -std=c11 -fsanitize=address,undefined -g`.
