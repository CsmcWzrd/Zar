# block_store / ZAR library

Production-oriented C++11 implementation of the ZAR block archive format.

## Build

```bash
make
```

## Run examples

```bash
./bin/example_write
./bin/example_write --text-size 65536
./bin/example_write --input-file ./path/to/input.bin --title payloads/input.bin
./bin/example_write --archive sample_be.zar --file-byte-order big --text-size 1048576
./bin/example_read
```

`example_write` now supports either generating text payloads of a caller-selected size or ingesting payload bytes from a file passed on the command line. Run `./bin/example_write --help` for the full option list.

## Notes

- Thread-safe public API
- Large offsets via `uint64_t`
- Host/file endianness support with configurable on-disk byte order
- No external JSON library required
- JSON outputs store binary payload as base64

## API summary

See `include/zar_store.h` and `docs/design.md`.

## Endianness

The library stores numeric header and index fields using a configurable on-disk byte order.
By default, archives are written in little-endian form for portability.

```cpp
zar::Store store;
store.setFileByteOrder(zar::kLittleEndian); // default
// or store.setFileByteOrder(zar::kBigEndian);
```

`getHostByteOrder()` reports the current machine byte order.

## Endianness conversion tool

```bash
make
./bin/zar_endian_convert --input archive_be.zar --output archive_le.zar --from big --to little
./bin/zar_endian_convert --input archive_le.zar --output archive_be.zar --from little --to big
```

The converter rewrites the archive with the requested on-disk byte order, preserving record order, header metadata, checksums, titles, and binary payloads.
