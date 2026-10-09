# Configuration and resource contracts

The engine consumes text configuration, structured binary storage, package archives, maps, images and sound resources. Their roles and readers differ. This guide records contracts visible in source and existing regressions; it is not a complete specification for an external game-resource editor.

## Configuration trees and text

[CBlockPar](../MatrixLib/Base/CBlockPar.hpp) owns an ordered tree of parameters and child blocks. Duplicate names are allowed. Indexed access and named/path access serve different purposes; converting the tree to a dictionary would discard order and duplicate entries. `LoadFromText` clears the destination before parsing, including empty input. `LoadFromTextFile` skips parsing when the file contains no payload, leaving the destination unchanged. `CStorage::RestoreBlockPar` appends to its destination, so restore into an empty tree unless appending is intended.

[CBlockPar.cpp](../MatrixLib/Base/CBlockPar.cpp) contains `BPCompiler`, text I/O and `ParamParser`. Named `key=value` parameters and brace-delimited blocks are used throughout [CFG/robots](../MatrixGame/CFG/robots). Text loading recognizes a leading UTF-16LE BOM on an even-sized Windows file; the other path converts bytes through the UTF-8/UTF-16 codecvt helpers. Do not silently change encoding or normalize existing localization data.

Values remain strings until a caller asks for a bool, integer, floating value, hex value or delimiter-separated component. These helpers are legacy parsers, not a general strict validation API. For example, `GetInt` skips non-digit characters and treats a minus sign anywhere as a negative sign. New strict options should validate explicitly while preserving existing callers' accepted input.

During standalone startup, [CGame::Init](../MatrixGame/src/MatrixGame.cpp) looks for `CFG/robots.dat`; DLL startup uses `CFG/<lang>/robots.dat` when the host supplies a language. A present file must load successfully; an unsupported packed format is an error. If it is absent, the engine loads `CFG/robots/data.txt`. Packed startup restores the `da` tree and reads `CFG/robots/cfg.txt` when present. Nonempty text in that file clears and replaces the restored `Config` block rather than merging entries; an empty file leaves it unchanged. Interface loading uses packed `if` or the tracked interface text according to the same configuration selection. Merely placing new text beside a present packed configuration does not select text-only startup.

## STRG storage and packed configuration

[CStorage.cpp](../MatrixLib/Base/CStorage.cpp) serializes typed record/column buffers. The supported Windows x86 representation uses little-endian 32-bit metadata and two-byte `wchar_t` strings. `CBuf` writes native scalar layouts; an x64 or endian migration needs an explicit disk-layout design.

| Header field | Encoding |
| --- | --- |
| Tag | 32-bit `0x47525453`, bytes `STRG` |
| Version | 32-bit zero for an uncompressed body, one for a compressed body; values greater than one are rejected |
| Body | Record count and named typed columns, directly for version zero or inside `ZL03` for version one |

`ZL03` begins with four tag bytes and a 32-bit block count. The writer splits input into chunks of at most 65,000 bytes, records each compressed chunk's 32-bit length and uses zlib compression. Column loading also accepts this framing, but whole-storage `Save` writes individual columns uncompressed before optional compression of the entire body. Keep tag/version values, field widths, string representation and decompressed content compatible when changing libraries; compressed bytes need not be identical across library versions.

`StoreBlockPar` represents each configuration block with four `ST_WCHAR` columns: `0`/`1` contain parameter names/values, and `2`/`3` contain child names/referenced record names. Child trees live in generated numeric records. The planetary `robots.dat` is this storage representation with roots including `da` and `if`; it is distinct from the original game's other `.dat` configuration formats.

Storage has single ownership and supports moves. Copying a `CStorageRecord` recreates an empty schema rather than cloning column data. Record deletion uses swap-with-last ordering. Pointers returned by `GetBuf` borrow their buffers and must not outlive the owning storage/record; pointers into `CBuf` data can be invalidated by growth. See [storage_tests.cpp](../tests/storage_tests.cpp) for these contracts, a frozen version-zero byte fixture, UTF-16/duplicate-parameter round trips, compression and selected rejected/partial-load cases.

For a local tracked-configuration round trip, run after building the corresponding tests, from the repository root:

```powershell
.\build\mingw-debug-exe\tests\matrixgame_storage_tests.exe manual.storage.buildcfg
.\build\mingw-release-exe\tests\matrixgame_storage_tests.exe manual.storage.buildcfg
```

This parses tracked interface/data configuration and packs/restores the trees in memory. It leaves supplied `robots.dat` and archives unchanged. Full details are in [Testing](TESTING.md).

## Files and package archives

[CFile::OpenRead](../MatrixLib/Base/CFile.cpp) first attempts a real file and then searches the registered package collection. This lets loose resources override archived content. Relative names use the process working directory, which standalone `WinMain` changes to the executable directory before loading resources. File-existence lookup also matches suffixes: a lone `robots.dat.backup` can make packed configuration appear present, but startup then opens the original `robots.dat` name and fails with `Error open file: cfg\robots.dat`. Use a different filename prefix when testing absence.

[Pack.hpp](../MatrixLib/Base/Pack.hpp) and [Pack.cpp](../MatrixLib/Base/Pack.cpp) implement folder/file records, virtual handles and compressed reads. A package starts with a 32-bit offset to its root folder, whose header records directory size, record count and record size. `SFileRec` is packed to one-byte alignment and contains fixed-size names, enum/32-bit metadata and pointer-width fields. Folder loading requires its stored record size to match `sizeof(SFileRec)`. Preserve the current x86 layout and packing; changing pointer width, names or enum size changes this contract. The collection encodes package/file identity in virtual handles, and each package has 16 virtual file-handle slots.

Open package-backed `CFile` objects must close before `CFile::ReleasePackFiles`; Debug checks track outstanding references. Read/write `CFile::Open` targets real files, while package fallback belongs to read access. Package paths and real filesystem paths are separate namespaces, so use the engine's reader when testing archived resources.

## Maps and images

[Common.hpp](../MatrixGame/src/Common.hpp) defines map table/column/property names, and [MatrixMapPrepare.cpp](../MatrixGame/src/MatrixMapPrepare.cpp) interprets them. A map is structured storage whose `properties` table contains `Name` and `Value` string columns. `SizeInUnitsX` and `SizeInUnitsY` are required during preparation. Other tables describe terrain/groups, scenery objects, buildings, robots, cannons and effects through typed columns and packed payloads. A successful generic storage load does not establish a valid map schema.

`PrepareMap` consumes configuration/resource data and uses an active DirectX device while constructing textures, geometry and battle objects. Full map preparation is therefore an integration procedure. Generic storage round trips cover container behavior; the automated suite does not yet freeze a complete map schema or every archived resource layout.

[FilePNG.cpp](../MatrixLib/Bitmap/FilePNG.cpp) adapts in-memory PNG bytes through libpng. `ReadStart_Buf` borrows the input bytes and returns an opaque handle, dimensions and a format code: gray, RGB, RGBA or palette. Sixteen-bit samples are stripped to eight bits. `Read` consumes that handle, fills caller-provided rows/palette and releases reader state, including on a caught decode error. Keep the input alive until that call finishes and provide sufficient row stride/output storage. The wrapper's writer supports additional formats, so writer support alone does not establish reader support for each combination.

## Compatibility evidence and limits

Standalone sound uses a private UTF-16 `Sound` mapping from logical event suffixes to archived WAV paths. The production [WAV reader](../MatrixGame/src/Audio/Audio.cpp) accepts mono/stereo PCM8/16, checks RIFF/chunk/frame metadata and supports bounded legacy header/padding variations found in original clips. [Standalone sound](AUDIO.md) defines those variants, resource preparation and the separation between sound effects and unsupported music/Ogg playback. Synthetic audio cases exercise the reader without private clips.

Frozen checksum/storage bytes and synthetic tree/column/compression cases are reproducible without private packages. Tracked text configuration can be exercised by the manual in-memory packing check. Live map loading/rendering requires the separately supplied game resources and the [standalone playtest](PLAYTESTING.md).

The existing readers validate some tags, types and lengths, but these checks do not establish rejection of every truncated, oversized or adversarial input. The initial engine suite has no complete package/PNG malformed-input coverage or full map-schema fixture. Add bounded synthetic cases when changing those readers or their dependencies; preserve original packages unchanged and keep payloads out of commits and test artifacts.
