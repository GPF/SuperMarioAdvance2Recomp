# SMA2 Dreamcast Dependency and Ownership Map

GBARecomp remains a separate framework dependency. Keep reusable CPU analysis, code generation, GBA runtime services, and KOS platform APIs there. SMA2 owns title startup/configuration and title-specific validation records. Do not copy shared GBARecomp runtime code into this repository.

## Submodule state

| Item | Value |
|---|---|
| Submodule path | `gbarecomp` |
| URL in `.gitmodules` | `https://github.com/GPF/gbarecomp.git` |
| SMA2 pinned gitlink | `647e2ab8c69666a55d04d82437002f1f1772c8bf` |
| Current submodule state | Uninitialized; `git submodule status` prefixes the pin with `-` |
| GBARecomp checkout used for first probe | `/home/gpf/code/gbarecomp` at `5e89943528aee7bc1c399bdbd035364a4b41dd10` |

The working checkout used for the probe differs from SMA2's pinned gitlink. The probe result is evidence for the exact checkout and fixture described in its report, not proof that the pinned revision supports the same workflow. Do not initialize, update, or repin this submodule without explicit authorization.

## Ownership

| Area | Owner | Current evidence/status |
|---|---|---|
| ARM/Thumb analysis and shared code generation | GBARecomp | Shared framework dependency; title consumes it. |
| SH-4 backend, generated ABI, dispatch | GBARecomp | First one-function static-link fixture passed at the recorded separate checkout revision. Two-function dispatch/call behavior is next; no game backend is established. |
| GBA CPU state, bus, cycles, DMA, IRQ, PPU, audio, save devices | GBARecomp | Shared runtime responsibilities. KOS runtime integration remains unverified. |
| KOS startup and reusable device/platform adapters | GBARecomp | Proposed shared platform responsibility; no full SMA2 integration has been demonstrated. |
| SMA2 startup, title configuration, ROM identity, code-copy hints | SMA2 | Defined by title sources and `game.toml`. |
| Host presentation/input | Shared interfaces and host implementation | Existing host path uses SDL2; KOS replacements and fidelity remain unverified. |
| Dynamic code and unresolved branches | Shared runtime policy plus title coverage configuration | Must make fallback and invalidation visible; Dreamcast behavior remains unresolved. |

No mGBADC, gpSP, or Bruce runtime code should be copied into SMA2. The isolated first-probe harness used stubs; it was not the real runtime.
