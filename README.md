# STM32 UART Bootloader with MD5 Verification

An archived STM32F446RE bootloader study covering UART firmware transfer, packet integrity, and whole-image MD5 verification. This repository is retained as a compact technical reference; it is not presented as an original end-to-end personal project.

## Protocol overview

The host and target exchange framed packets with a sync byte, command, payload length, payload, and CRC16-CCITT. The command set supports:

- link testing;
- image MD5 comparison;
- flash-session start;
- chunked firmware transfer;
- transfer completion and application handoff.

The target-side code reserves the application region at `0x08008000`. Review the linker configuration and memory map for your own MCU before using it on hardware.

## Repository layout

- `include/` and `src/` — reusable bootloader and MD5 implementation.
- `tools/host_flasher.py` — `.hex`/`.bin` uploader with CRC and MD5 checks.
- `tools/serial_monitor.py` — serial diagnostics.
- `examples/minimal/` — integration excerpts for bootloader and application projects.
- `docs/PROJE_ALGORITMA_OZETI.md` — original Turkish algorithm notes.

Generated STM32CubeIDE projects, vendor HAL copies, and firmware samples are intentionally excluded to keep the reference focused.

## Host setup

```bash
python -m venv .venv
# Windows: .venv\Scripts\activate
# Linux/macOS: source .venv/bin/activate
python -m pip install -r requirements.txt
python tools/host_flasher.py --help
```

Example:

```bash
python tools/host_flasher.py firmware.bin --port COM10 --baud 115200
```

## Integration notes

1. Configure the bootloader and application flash regions in the linker scripts.
2. Copy the reusable sources into the STM32 project and connect them to the intended UART HAL handle.
3. Validate erase boundaries, image-size limits, vector-table relocation, and jump behavior.
4. Exercise recovery paths before deploying to non-recoverable hardware.

This code is an educational reference and has not been presented as a production-secure update mechanism. MD5 detects accidental image differences but is not a substitute for authenticated firmware signing.
