# Where Credentials Live

`src/board/flash_block.cpp` owns the last 8 KB erase block of the SAMD51's
512 KB flash, at a hardcoded `0x7E000`.

The address is pinned rather than left to the linker for a specific reason.
`FlashStorage_SAMD`'s macros place their storage inside `.text`, which puts it
within the range `bossac` rewrites on upload *and* lets the address drift
whenever the program's size changes. Either one destroys stored credentials on a
firmware update, silently. At `0x7E000` — some 460 KB above `__etext` — the block
is never written by an upload and never moves, so pairings survive `-t upload`.

`CredentialStore::begin()` logs a FATAL line if the image ever grows into that
block, or if the flash page geometry is not what the code assumes. Writes are
read-back verified.
