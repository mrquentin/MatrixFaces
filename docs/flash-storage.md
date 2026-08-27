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

`CredentialStore::begin()` and `AppSettingsStore::begin()` each log a FATAL
line if the image ever grows into their respective block, or if the flash
page geometry is not what the code assumes. Writes are read-back verified.

App settings are additionally keyed by app name and setting key inside their
blob, not by app registration index -- see [API reference](api.md#app-settings)
for why.
