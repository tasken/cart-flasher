<p align="center">
  <img src="resources/logo.png" alt="Cart-Flasher Logo" width="128">
</p>

# Cart-Flasher

Back up and restore Slot-1 flashcart firmware on DS and DSi.

## Getting started

Download the latest [`cart_flasher.nds`](https://github.com/tasken/cart-flasher/releases/latest/download/cart_flasher.nds) and place it on your flashcart's SD card.

1. Open your flashcart menu and launch Cart-Flasher.
1. Select your cart, then choose `Back up flash` to save it in `cart-backups`.
1. Copy your first backup off the SD card before using `Write flash`.
1. To restore a flashrom, choose `Write flash`, select its `.bin` backup, and enter the displayed key combo.

> [!TIP]
> Each cart has one backup filename. A new backup replaces it, so keep a copy off the SD card.

> [!NOTE]
> `Back up DS banner` and `Write DS banner` appear only for a recognized safe layout. Banner backups go in `cart-backups/banners` and are not replaced. Create or edit a banner with [DS Banner Maker](https://tasken.github.io/banner-maker/). Cart-Flasher changes and verifies only the banner area.

## Supported carts

Ace3DS+, Acekard 2i, DSTT, R4i Gold 3DS, R4iSDHC family, R4 SDHC Dual-Core (DSi mode only), and Datel carts (Games N' Music, Max Media Player, Action Replay DS).

> [!NOTE]
> Ace3DS+ banner options appear only for recognized firmware layouts. The 2 MiB TH25Q16/ADLE layout is supported; an unrecognized layout safely shows only flash operations.
>
> `Try alternate detection` is only for known 2 MiB R4iSDHC.hk Dual Core 2021 profiles: Deep Labyrinth/ADLE or SpongeBob/AL3E. Start with `Back up flash`, and restore only a verified image.

See `Sanras`'s [flashcart guide](https://sanrax.github.io/flashcart-guides/) and [banner tutorial](https://sanrax.github.io/flashcart-guides/tutorials/icon-change/).

> [!CAUTION]
> **Breaks stock DSi/3DS compatibility**
>
> Changing a flashcart's icon or banner text prevents stock DSi and 3DS firmware from launching it. Use custom firmware (CFW) on those consoles; DS and DS Lite are unaffected. This applies only to changed banners, not your untouched backup.

> [!WARNING]
> Not every cart has been tested. If `Back up flash` fails, has an unexpected size, or cannot be read, **STOP**. Do not use `Write flash`. [Open an issue](https://github.com/tasken/Cart-Flasher/issues) with your cart and launch details.
>
> Test on real DS-family hardware. Flashcart detection in an emulator is not valid. Flashing carries a risk, including a bricked cart.

## Reporting a problem

Press `Y` on the cart list until the log reads `DEBUG`, reproduce the problem once, then [open an issue](https://github.com/tasken/Cart-Flasher/issues) with `cart-backups/cart_flasher.log`, your cart model, and how you launched Cart-Flasher.

> [!NOTE]
> If `SD card init failed!` appears, take a photo of the screen instead. There is no log.

## Building

```shell
git clone https://github.com/tasken/cart-flasher.git
cd cart-flasher/
sudo ./build.sh
```

This builds `cart_flasher-dev.nds` with Docker and BlocksDS. `build.sh` initializes the pinned submodules and refreshes the builder. Run `sudo ./build.sh clean` to remove build outputs without refreshing it. With a local BlocksDS install, run `git submodule update --init --recursive` once, then use `make`.

Local Debug builds include a deterministic simulated cart for flash and banner flows. It never sends Slot-1 commands; nightly and release builds exclude it.

## Credits

*   Developed by `Tasken` (`Nimbo` on Discord)
*   Upstream base: repurposed as a general-purpose flashcart dump/write tool, built on top of:
    *   [ntrboot_flasher_nds](https://github.com/jason0597/ntrboot_flasher_nds) by `jason0597` (original project)
    *   [ntrboot_flasher_nds](https://github.com/DS-Homebrew/ntrboot_flasher_nds) by `DS-Homebrew` (enhanced fork)
*   Drivers & core:
    *   [flashcart_core](https://github.com/ntrteam/flashcart_core) by `ntrteam`, for the per-flashcart device drivers
    *   [libncgc](https://github.com/angelsl/libncgc) by `angelsl`, for the NTR/CTR card protocol layer
    *   [datelTool](https://github.com/ApacheThunder/datelTool) by `ApacheThunder`, for Datel cart support; AUXSPI protocol logic by `edo9300`

Special thanks to `Sanras` for feedback and pre-release testing, and `ApacheThunder` for Datel hardware testing. Make sure to check out Sanras's [flashcart guide](https://sanrax.github.io/flashcart-guides/).

The key combo confirmation before writing to a cart is styled after `d0k3`'s [GodMode9](https://github.com/d0k3/GodMode9) unlock sequence prompt.

## License

GPL-3.0 - see [LICENSE](LICENSE).

Copyright © the original `ntrboot_flasher_nds` authors and later contributors (see Credits above, and the git history for per-change authorship). The vendored `flashcart_core` and `libncgc` are also GPL-3.0, under their own licenses.
