<p align="center">
  <img src="resources/logo.png" alt="Cart-Flasher Logo" width="128">
</p>

# Cart-Flasher

A DS/DSi homebrew application to backup and restore raw flash images to/from Slot-1 flashcarts.

## Getting started

Download the latest [`cart_flasher.nds`](https://github.com/tasken/cart-flasher/releases/latest/download/cart_flasher.nds) and place it on your flashcart's SD card.

1. Open your flashcart menu and launch Cart-Flasher.
1. Read the warning, then press `A` to continue.
1. Select your cart from the list.
1. Select `Back up flash`, then press `A` to save a backup in `cart-backups` on the SD card.
1. Copy your first backup off the SD card before using `Write flash`.
1. To restore a flashrom, select `Write flash`, choose the `.bin` backup, then enter the displayed key combo.
1. When available, select `Back up DS banner` to save the current banner in `cart-backups/banners`.
1. Select `Write DS banner`, choose a banner from `cart-backups/banners`, then enter the displayed key combo.
1. When the completion screen appears, press `A` to return to the cart list.

> [!TIP]
> Flash backups use one filename per cart. Backing up the same cart again
> replaces the existing backup, so keep a copy somewhere off the SD card.

> [!NOTE]
> Banner options appear only when Cart-Flasher recognizes a safe layout.
> Banner backups go in `cart-backups/banners`; the app keeps older banner
> backups instead of replacing them. Use
> [DS Banner Maker](https://tasken.github.io/banner-maker/) to create a new
> banner or edit a backup, then select it with `Write DS banner`.
>
> Before writing, the app checks the cart and banner, changes only the banner
> area, and verifies the result afterward.

## Supported carts

Ace3DS+, Acekard 2i, DSTT, R4i Gold 3DS, R4iSDHC family, R4 SDHC Dual-Core
(DSi mode only), and Datel carts (Games N' Music, Max Media Player, Action
Replay DS).

> [!NOTE]
> Ace3DS+ banner options appear only for recognized firmware layouts. The
> 2 MiB TH25Q16/ADLE layout is supported; an unrecognized layout safely shows
> only flash operations.
>
> If normal Ace3DS+ detection fails, select `Try alternate detection` only for
> the known 2 MiB R4iSDHC.hk Dual Core 2021 (Deep Labyrinth/ADLE) or SpongeBob
> (AL3E) stock profiles. Start with `Back up flash`, and restore only a
> verified image.

`Sanras`'s [flashcart guide](https://sanrax.github.io/flashcart-guides/) covers which retail carts these map to, and has a full walkthrough for [changing a flashcart's banner](https://sanrax.github.io/flashcart-guides/tutorials/icon-change/) using this tool.

> [!CAUTION]
> **Breaks stock DSi/3DS compatibility**
>
> Changing a flashcart's icon or banner text prevents it from launching on
> stock DSi and 3DS firmware. Use CFW (Custom Firmware) on those consoles;
> DS and DS Lite are unaffected.
>
> This only applies to banners you have changed. Restoring your own untouched backup is fine.

> [!WARNING]
> Not every cart has been tested. If `Back up flash` fails, or its backup has
> an unexpected size or cannot be read, **STOP**. Do not use `Write flash`.
> [Open an issue](https://github.com/tasken/Cart-Flasher/issues) with your
> cart and launch details.
>
> Test only on real DS-family hardware. Flashcart detection in an emulator is
> not valid.
>
> And as always, flashing carts and modifying firmware carries a risk. We are not responsible for any damage that may occur, such as bricked carts.

## Reporting a problem

1. Press `Y` on the cart list until the log reads `DEBUG`.
1. Reproduce the problem once.
1. Power off and copy `cart-backups/cart_flasher.log` from the SD card.
1. [Open an issue](https://github.com/tasken/Cart-Flasher/issues) with the
   log, cart model, and how you launched Cart-Flasher.

> [!NOTE]
> If you saw `SD card init failed!`, there is no log to send. Take a photo of the screen and attach that instead.

## Building

```shell
git clone https://github.com/tasken/cart-flasher.git
cd cart-flasher/
sudo ./build.sh
```

This builds inside Docker, including the BlocksDS toolchain, and produces
`cart_flasher-dev.nds`. Nightlies are `cart_flasher-nightly-<commit>.nds`;
releases are `cart_flasher.nds`. If BlocksDS is already installed locally, run
`git submodule update --init --recursive` once, then use `make` directly.
`build.sh` initializes both pinned core submodules, pulls the current builder
base image, and refreshes BlocksDS packages without Docker layer cache.
CI refreshes the same BlocksDS packages before compiling.

Local builds use build kind `Debug` and include a software-only simulated cart.
It exercises full flash and DS banner operations with deterministic data,
starts with the Cart-Flasher DS banner, and never sends Slot-1 commands.
Nightly and release builds use explicit non-Debug kinds and never include it.

## Credits

*   Developed by `Tasken` (`Nimbo` on Discord)
*   Upstream base: repurposed as a general-purpose flashcart dump/write tool, built on top of:
    *   [ntrboot_flasher_nds](https://github.com/jason0597/ntrboot_flasher_nds) by `jason0597` (original project)
    *   [ntrboot_flasher_nds](https://github.com/DS-Homebrew/ntrboot_flasher_nds) by `DS-Homebrew` (enhanced fork)
*   Drivers & core:
    *   [flashcart_core](https://github.com/ntrteam/flashcart_core) by `ntrteam`, for the per-flashcart device drivers
    *   [libncgc](https://github.com/angelsl/libncgc) by `angelsl`, for the NTR/CTR card protocol layer
    *   [datelTool](https://github.com/ApacheThunder/datelTool) by `ApacheThunder`, for Datel
        cart support; AUXSPI protocol logic by `edo9300`

Special thanks to `Sanras` for feedback and pre-release testing, and
`ApacheThunder` for Datel hardware testing. Make sure to check out Sanras's
[flashcart guide](https://sanrax.github.io/flashcart-guides/).

The key combo confirmation before writing to a cart is styled after `d0k3`'s [GodMode9](https://github.com/d0k3/GodMode9) unlock sequence prompt.

## License

GPL-3.0 - see [LICENSE](LICENSE).

Copyright © the original `ntrboot_flasher_nds` authors and later contributors
(see Credits above, and the git history for per-change authorship). The vendored
`flashcart_core` and `libncgc` are also GPL-3.0, under their own licenses.
