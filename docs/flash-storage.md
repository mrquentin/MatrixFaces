# Where Credentials and Settings Live

Two things outlive a reboot: paired client credentials (`CredentialStore`) and
app settings (`AppSettingsStore`). Both are written through the same two-layer
arrangement, which is what lets the boards disagree about storage hardware
without disagreeing about a single stored byte.

- `src/storage/record_blob.cpp` frames a fixed-capacity array of POD records as
  magic / version / count / records / CRC32. It is pure, host-tested, and
  identical on every board.
- `src/board/blob_store.h` names two blobs — `creds` and `settings` — and does
  nothing but move their bytes to wherever the board keeps them.

A new persisted concern is a new blob name and a new `record_blob` layout, not
a new copy of the framing and CRC plumbing.

## On the M4: raw flash

`src/board/samd51/flash_block.cpp` owns raw access to the last two 8 KB erase
blocks of the SAMD51's 512 KB flash, at hardcoded addresses: `0x7E000` for
credentials, `0x7C000` for settings. Each concern gets its own block so writing
one never cycles the other's erase count.

The addresses are pinned rather than left to the linker for a specific reason.
`FlashStorage_SAMD`'s macros place their storage inside `.text`, which puts it
within the range `bossac` rewrites on upload *and* lets the address drift
whenever the program's size changes. Either one destroys stored data on a
firmware update, silently. At `0x7C000`/`0x7E000` — several hundred KB above
`__etext` — neither block is ever written by an upload nor moves, so both
credentials and settings survive `-t upload`.

`flash_block::erasedWrite()` erases the whole target block once, then writes it
back one 512-byte NVMCTRL page at a time — the page buffer only ever holds one
page, so anything larger than a page has to be committed in that many separate
clear/fill/write-page cycles. A blob that fit in one page always worked; one
that didn't (an early version of the settings blob, at ~2.3 KB) silently failed
every write and quietly ran with un-persisted defaults. `erasedWrite()` now
accepts anything up to a full erase block.

Raw flash stores no length: a load reads back exactly as many bytes as the
caller asked for, an erased block reads as all `0xFF`, and `record_blob::parse`
is what tells "blank" from "corrupt" from "real".

## On the S3: NVS

`src/board/esp32s3/blob_store.cpp` puts the same two blobs in an NVS namespace
(`matrixfaces`) in the partition reserved by
`boards/partitions_8mb_ota.csv`. Twenty lines, because the wear levelling, the
checksums and the atomic replace are things NVS already does.

A key that was never written reports itself the way an erased flash block does
— `0xFF` up to the requested length — so both boards take the same path through
`record_blob::parse` on a blank device rather than one of them having a special
case.

NVS lives in its own partition and is not touched by a firmware upload, so
settings and pairings survive a reflash. They do *not* survive a change to the
partition table, which is why that table was chosen once, up front, with room
for everything phases 5 and 6 will need.

Contents are stored in the clear; see [security.md](security.md#pairing) for
why that is a deliberate choice rather than an omission.

## Both boards

`CredentialStore::begin()` and `AppSettingsStore::begin()` each call
`blob_store::checkPlacement()`, which logs a FATAL line if the image has grown
into the M4's storage block or the flash geometry is not what the driver
assumes, and on the S3 if the NVS namespace will not open at all. Writes are
verified.

App settings are additionally keyed by app name and setting key inside their
blob, not by app registration index — see
[API reference](api.md#app-settings) for why.
