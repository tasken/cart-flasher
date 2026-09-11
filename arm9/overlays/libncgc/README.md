# Cart-Flasher libncgc overlay

`vendor/libncgc` is pristine upstream `166dc42`. Before compilation,
`scripts/prepare_libncgc.sh` copies it to `generated/libncgc` and overlays
`files/`.

The overlay contains required BlocksDS build support, protocol fixes, and
controlled SPI support for Datel. Keep the submodule pristine. Add only
unavoidable core changes as clean files below `files/`.

## NTR detection timeouts

The NTR platform has bounded ROM and AUXSPI transfer waits. Cart-Flasher arms
an additional five-second deadline only while detecting a physical cart,
including an alternate recovery attempt. A stalled transport returns
`NCGC_ETIMEOUT`, clears its host transfer, and lets the app return to its
existing detection-failure screen. Backup and write operations run after that
detection deadline is disarmed.
