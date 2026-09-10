# Cart-Flasher flashcart_core overlay

`vendor/flashcart_core` is pristine upstream `v1.1.0`. Before ARM9
compilation, `scripts/prepare_flashcart_core.sh` copies it to
`generated/flashcart_core` and overlays `files/`.

The overlay contains required core fixes and drivers. Banner profiles and UI
stay in `source/banner_ops.cpp`.

Keep the submodule pristine. Add only unavoidable core changes as clean files
below `files/`.

## Ace3DS+ TH25Q16 support

The Ace3DS+ driver recognizes the observed `EB 60 15` wire-order RDID as
TH25Q16-class 2 MiB serial flash and records that identification in the
hardware log. It continues to use the existing `0x15` capacity case, which
backs up exactly 2 MiB.

`readFlash()` rejects a request that would exceed the initialized chip
capacity. This guards the raw three-byte SPI read against a caller reading
past the end of this identified device. It does not change any erase, program,
or restore behavior.

The exact `EB 60 15` cart completed a same-image 2 MiB write, matching full
readback, and cold boot. It therefore uses the normal full-image write path.
Its banner profile is app-owned in `source/banner_ops.cpp`, where the complete
page map, `ADLE` header, virtual banner pointer, and current banner CRC must
match before it reads, modifies, or verifies raw block `0x120000-0x120FFF`.
