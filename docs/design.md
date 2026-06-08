# ZAR C++11 library design

## Goals

This implementation targets:

- C++11 compatibility
- large file offset support through 64-bit offsets
- thread-safe public API via `std::mutex`
- no external dependencies
- streaming JSON output for large result sets
- Windows and POSIX path handling
- configurable on-disk endianness with host-endian conversion

## Public API

Main class: `zar::Store`

- `open(path, createIfMissing)`
- `setFileByteOrder(order)`
- `getFileByteOrder()`
- `getHostByteOrder()`
- `save()`
- `close()`
- `append(header, title, data, outId, error)`
- `SelectOne`
- `SelectOneToJsonFile`
- `SelectAll`
- `SelectAllToJsonFile`
- `SelectReverseOne`
- `SelectReverseOneToJsonFile`
- `SelectReverseAll`
- `SelectReverseAllToJsonFile`
- `ExtractOne`
- `ExtractAll`

## Header accessors

`zar::HeaderView` wraps the packed 256-byte `Header` and provides explicit
getters/setters for every field, including per-counter access.

## Record model

For each stored object:

- `title` is stored first in payload
- `data` follows
- header stores `titleLength` and `dataLength`
- checksum is computed over `title + data`

## Reverse search

Reverse selectors iterate over the in-memory index vector from the end toward
its beginning and apply the same query matcher.

## Notes

- `m_prevBlock` and `m_nextBlock` describe the contiguous record chain.
- Continuation blocks do not contain separate headers.
- Query fields `timestamp` and `id` support inclusive ranges.
- Query fields `flags`, `checksum`, `additional`, and `extra` support exact
  matching.

## Endianness

All numeric header fields and index entries are converted between the host machine byte order and the configured archive byte order during I/O. Payload bytes, checksum bytes, and additional bytes are stored exactly as provided. The default archive byte order is little-endian so files remain portable across common x86/x64 systems, while big-endian targets can opt into big-endian archives explicitly.
