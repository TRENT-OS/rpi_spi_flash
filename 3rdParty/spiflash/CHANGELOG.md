# Changelog
All notable changes by HENSOLDT Cyber GmbH to this 3rd party module included in
the TRENTOS-M SDK will be documented in this file.

For more details it is recommended to compare the 3rd party module at hand with
the previous versions of the TRENTOS-M SDK or the baseline version.

## [1.0]
### Changed
- Adapt module for the Raspberry Pi 3 Model B+ platform.
- Disable `SPIFLASH_OP_WRITE_sDATA` case in `_spiflash_begin_async` and
`_spiflash_end_async`.
- Apply our coding style / formatting.

### Added
- Start integration of spiflash based on commit 91b086 of
https://github.com/pellepl/spiflash_driver.
