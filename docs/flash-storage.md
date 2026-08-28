# Where Credentials and Settings Live

`src/board/flash_block.cpp` owns raw access to the last two 8 KB erase blocks
of the SAMD51's 512 KB flash, at hardcoded addresses: `0x7E000` for paired
client credentials (`CredentialStore`), `0x7C000` for app settings
(`AppSettingsStore`). Each concern gets its own block so writing one never
cycles the other's erase count.

The addresses are pinned rather than left to the linker for a specific
reason. `FlashStorage_SAMD`'s macros place their storage inside `.text`, which
puts it within the range `bossac` rewrites on upload *and* lets the address
drift whenever the program's size changes. Either one destroys stored data on
a firmware update, silently. At `0x7C000`/`0x7E000` — several hundred KB above
`__etext` — neither block is ever written by an upload nor moves, so both
credentials and settings survive `-t upload`.

`flash_block::erasedWrite()` erases the whole target block once, then writes
it back one 512-byte NVMCTRL page at a time — the page buffer only ever holds
one page, so anything larger than a page has to be committed in that many
separate clear/fill/write-page cycles. A blob that fit in one page always
worked; one that didn't (an early version of `AppSettingsStore`'s, at ~2.3 KB)
silently failed every write and quietly ran with un-persisted defaults, since
the single-page-only version of this function rejected it outright before
touching the flash. `erasedWrite()` now accepts anything up to a full erase
block (`kBlockSize`).

`CredentialStore` and `AppSettingsStore` each own a
`FlashRecordStore<Record, kMaxRecords>` (`src/board/flash_record_store.h`) at
their respective address, rather than hand-rolling their own
magic/version/count/records/crc blob and CRC32 routine. `FlashRecordStore`
owns exactly that layout, the checksum, and the read-back-verified write; it
has no idea what a `StoredClient` or a per-app setting *means* — matching a
loaded record back to a live client or app is still each owner's job. A new
persisted concern (e.g. more than the pairing secret alone) is a new
`FlashRecordStore<Record, N>` member at a new `flash_block` address, not a
new copy of the blob/CRC plumbing.

`CredentialStore::begin()` and `AppSettingsStore::begin()` each call
`checkPlacement()` on their store, which logs a FATAL line if the image ever
grows into their respective block, or if the flash page geometry is not what
the code assumes. Writes are read-back verified.

App settings are additionally keyed by app name and setting key inside their
blob, not by app registration index -- see [API reference](api.md#app-settings)
for why.
