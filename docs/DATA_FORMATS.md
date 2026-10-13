# Configuration and resource contracts

The engine consumes text configuration, structured binary storage, package archives, maps, images and sound resources. Their roles and readers differ. This guide records contracts visible in source and existing regressions; it is not a complete specification for an external game-resource editor.

## Configuration trees and text

[CBlockPar](../MatrixLib/Base/CBlockPar.hpp) owns an ordered tree of parameters and child blocks. Duplicate names are allowed. Indexed access and named/path access serve different purposes; converting the tree to a dictionary would discard order and duplicate entries. `LoadFromText` clears the destination before parsing, including empty input. `LoadFromTextFile` skips parsing when the file contains no payload, leaving the destination unchanged. `CStorage::RestoreBlockPar` appends to its destination, so restore into an empty tree unless appending is intended.

[CBlockPar.cpp](../MatrixLib/Base/CBlockPar.cpp) contains `BPCompiler`, text I/O and `ParamParser`. Named `key=value` parameters and brace-delimited blocks are used throughout [CFG/robots](../MatrixGame/CFG/robots). Text loading recognizes a leading UTF-16LE BOM on an even-sized Windows file; the other path converts bytes through the UTF-8/UTF-16 codecvt helpers. Do not silently change encoding or normalize existing localization data.

Values remain strings until a caller asks for a bool, integer, floating value, hex value or delimiter-separated component. These helpers are legacy parsers, not a general strict validation API. For example, `GetInt` skips non-digit characters and treats a minus sign anywhere as a negative sign. New strict options should validate explicitly while preserving existing callers' accepted input.

During standalone startup, [CGame::Init](../MatrixGame/src/MatrixGame.cpp) looks for `CFG/robots.dat`; DLL startup uses `CFG/<lang>/robots.dat` when the host supplies a language. A present file must load successfully; an unsupported packed format is an error. If it is absent, the engine loads `CFG/robots/data.txt`. Packed startup restores the `da` tree and reads `CFG/robots/cfg.txt` when present. Nonempty text in that file clears and replaces the restored `Config` block rather than merging entries; an empty file leaves it unchanged. Interface loading uses packed `if` or the tracked interface text according to the same configuration selection. Merely placing new text beside a present packed configuration does not select text-only startup.

Standalone resolution and window-mode overrides are read from the separate `CFG/standalone.txt` file and apply with either packed or text game data. They replace the embedded `Config` block's `Resolution`/`FullScreen` choices for the EXE. The DLL continues to use host-provided settings. See [Display settings](DISPLAY.md) for values and fallback behavior. The same file stores volume, shadow, sky, camera-speed and random-generator options; [Settings window](SETTINGS.md) lists every key and how the in-game window rewrites the file.

## STRG storage and packed configuration

[CStorage.cpp](../MatrixLib/Base/CStorage.cpp) serializes typed record/column buffers. The wire layout below is specified by field width and byte order, independently of native structure sizes or pointer width. All integers are little-endian; strings are UTF-16LE code units. `CBuf` writes native scalars, which match this layout on Windows x86 and x64 because the written types are fixed-width there; another platform or byte order needs explicit encoding.

| Offset | Field | Encoding |
| --- | --- | --- |
| 0 | Tag | 32-bit `0x47525453`, bytes `STRG` |
| 4 | Version | 32-bit zero for an uncompressed body, one for a compressed body; values greater than one are rejected |
| 8 | Body | Version zero: 32-bit record count followed by the records. Version one: a `ZL03` stream whose decoded bytes are the version-zero body |

A record is its name, a 32-bit column count and the columns. A column is its name, a 32-bit type, a 32-bit body length and the body. Names are UTF-16LE code units terminated by one zero unit; an empty name is a single zero unit. Lookups compare names exactly. Types are `0` `ST_INT32` (signed 32-bit), `1` `ST_DWORD` (unsigned 32-bit), `2` `ST_BYTE`, `3` `ST_FLOAT` (IEEE-754 binary32), `4` `ST_DOUBLE` (binary64) and `5` `ST_WCHAR` (UTF-16 code unit, no terminator). Bit 31 marks a compressed column body; the reader clears it after decoding.

A column body holds any number of element arrays:

| Body offset | Field | Encoding |
| --- | --- | --- |
| 0 | Table offset `T` | 32-bit offset of the array table from the body start |
| 4 | Array count `N` | 32-bit |
| 8 | Element size | 32-bit: 4, 4, 1, 4, 8 or 2 for types 0 to 5 |
| 12 | Elements | Array contents |
| `T` | Array table | `N` entries of three 32-bit values: element offset from the body start, element count and capacity |

The writer compacts each column before saving: arrays are contiguous in index order from offset 12, capacity equals count, `T` is 12 plus the element bytes and the body length is `T + 12N`. The reader copies the body verbatim.

`ZL03` begins with the tag bytes `ZL03` and a 32-bit block count. Each block is a 32-bit stream length followed by a zlib (RFC 1950) stream; the decoded blocks are concatenated. The writer splits input into 65,000-byte blocks, with a shorter final block, and compresses at level 9. The reader accepts shorter non-final blocks and rejects a block that decodes to more than 65,000 bytes. Whole-storage `Save` writes columns uncompressed and, for version one, compresses everything after the version field. `CStorageRecord::Save` can instead compress each column body, setting bit 31 and storing the `ZL03` stream as the body. Keep tag/version values, field widths, string representation and decompressed content compatible when changing libraries; compressed bytes need not be identical across library versions.

`StoreBlockPar` represents each configuration block as a record with four `ST_WCHAR` columns named `0` to `3`, each holding one array per entry in tree order: parameter names, parameter values, child block names and child record names. Records are written parent first. Each child record receives the smallest non-negative decimal name not already used as a record name, so a root `da` with children `A` (containing `Deep`) and `B` produces records `da`, `0`, `1` and `2`. `RestoreBlockPar` appends to its destination. The planetary `robots.dat` is this storage representation with roots including `da` and `if`; it is distinct from the original game's other `.dat` configuration formats.

`CStorage::Load` returns `false` for a wrong tag, a version greater than one or a compressed column whose stream cannot be decoded. It throws `Base::CException` for truncated input, counts or lengths beyond the remaining input, and version-one data with a short or mistagged `ZL03` header, no blocks, an oversized block or an invalid zlib stream. A storage object that rejected input can load valid input afterwards. The reader does not yet reject every malformed input safely: `ZL03` block counts and stream lengths are not checked against the available bytes before decoding, column bodies are not validated before their table offsets are used, and a record count reserves memory before the records are read. Do not load untrusted storage until those checks exist.

Storage has single ownership and supports moves. Copying a `CStorageRecord` recreates an empty schema rather than cloning column data. Record deletion uses swap-with-last ordering. Pointers returned by `GetBuf` borrow their buffers and must not outlive the owning storage/record; pointers into `CBuf` data can be invalidated by growth. See [storage_tests.cpp](../tests/storage_tests.cpp) for these contracts, a frozen version-zero byte fixture, UTF-16/duplicate-parameter round trips, compression and selected rejected/partial-load cases.

For a local tracked-configuration round trip, run after building the corresponding tests, from the repository root:

```powershell
.\build\mingw-debug-exe\tests\matrixgame_storage_tests.exe manual.storage.buildcfg
.\build\mingw-release-exe\tests\matrixgame_storage_tests.exe manual.storage.buildcfg
```

This parses tracked interface/data configuration and packs/restores the trees in memory. It leaves supplied `robots.dat` and archives unchanged. Full details are in [Testing](TESTING.md).

## Files and package archives

[CFile::OpenRead](../MatrixLib/Base/CFile.cpp) first attempts a real file and then searches the registered package collection. This lets loose resources override archived content. Relative names use the process working directory, which standalone `WinMain` changes to the executable directory before loading resources. File-existence lookup also matches suffixes: a lone `robots.dat.backup` can make packed configuration appear present, but startup then opens the original `robots.dat` name and fails with `Error open file: cfg\robots.dat`. Use a different filename prefix when testing absence.

[Pack.hpp](../MatrixLib/Base/Pack.hpp) and [Pack.cpp](../MatrixLib/Base/Pack.cpp) implement folder/file records, virtual handles and compressed reads. All package integers are little-endian 32-bit values and offsets are absolute file positions. A package starts with the offset of its root folder. A folder is a 12-byte header (directory size, record count and record size) followed by the records. The reader ignores the directory size and requires the record size to be 158.

| Record offset | Field | Encoding |
| --- | --- | --- |
| 0 | Size | Stored extent at the data offset, including its 4-byte prefix |
| 4 | Real size | Decoded file size |
| 8 | Lookup name | 63 bytes, NUL-terminated uppercase name |
| 71 | Real name | 63 bytes, NUL-terminated case-preserving name used in listings and folder construction |
| 134 | Type | `0` text, `1` binary, `2` compressed, `3` folder |
| 142 | Free flag | Nonzero marks an unused record that lookup, listing and loading ignore |
| 146 | Date | Not interpreted by the reader |
| 150 | Offset | Folder: nested folder header. File: data prefix |

The 32-bit fields at 138 (pending type) and 154 (runtime data) are replaced when a folder loads, so their stored values are ignored. Names hold at most 62 characters. A folder's lookup name must equal the uppercase form of its real name. Lookup splits paths on `/` or `\`, converts each component to uppercase and compares it with the lookup name.

File data begins with a 32-bit value (the size minus four). An uncompressed payload of real-size bytes follows. A compressed payload is a sequence of blocks, each a 32-bit block length followed by that many bytes: the tag `ZL02`, a 32-bit decoded length and a zlib stream. Every block except the last decodes to 65,536 bytes, and a block length must not exceed the 72,112-byte read buffer.

The reader's `SFileRec` matches this record only while its two `uintptr_t` fields are 32-bit; on the supported x86 build `sizeof(SFileRec)` is 158. Preserve the record layout and packing; changing pointer width, names or enum size must not change the stored record.

`OpenPacketFile` returns `false` and closes the file for an empty or short file, a root offset beyond the end, a short folder header, a record size other than 158, fewer records than declared, and the same faults in a nested folder; a collection open fails when any package fails and closes the packages already opened. The collection searches packages in registration order and encodes a virtual handle as the package slot plus the package index shifted left by four; each package has 16 slots. The reader does not yet reject every malformed package safely: folder offsets forming a cycle recurse without limit, names without a terminator inside their field are read past it, a folder whose lookup and real names disagree raises an error while the folder is destroyed, compressed block lengths are not checked against the read buffer, and uncompressed reads are not limited to the real size. Package records are trusted input until those checks exist.

Open package-backed `CFile` objects must close before `CFile::ReleasePackFiles`; Debug checks track outstanding references. Read/write `CFile::Open` targets real files, while package fallback belongs to read access. Package paths and real filesystem paths are separate namespaces, so use the engine's reader when testing archived resources.

## Maps and images

[Common.hpp](../MatrixGame/src/Common.hpp) defines map table/column/property names, and [MatrixMapPrepare.cpp](../MatrixGame/src/MatrixMapPrepare.cpp) interprets them. A map is structured storage whose `properties` table contains `Name` and `Value` string columns. `SizeInUnitsX` and `SizeInUnitsY` are required during preparation. Other tables describe terrain/groups, scenery objects, buildings, robots, cannons and effects through typed columns and packed payloads. A successful generic storage load does not establish a valid map schema.

`PrepareMap` consumes configuration/resource data and uses an active DirectX device while constructing textures, geometry and battle objects. Full map preparation is therefore an integration procedure. Generic storage round trips cover container behavior; the automated suite does not yet freeze a complete map schema or every archived resource layout.

Map payloads are `ST_BYTE` arrays that the preparation code reads as raw structures, and vector-object files are read the same way, so these compiled layouts are stored layouts. Their fields are fixed-width little-endian integers and binary32 values on Windows. `SCompilePoint` is 12 bytes (32-bit move index, binary32 height, then blue, green, red and flag bytes), `SCompileMove` 16, `SCompileMoveCell` 64 and `SCompileBottomVert` 8 (four 16-bit values). The vector-object records are `SVOHeader` 80 bytes with tag `0x00006f76`, `SVOVertexNorTex` 32, `SVOMaterial` 136, `SVOGroup` 32, `SVOFrame` 64, `SVOAnimHeader` and `SVOMatrixHeader` 80, `SVOFrameEdge` 8 and `SVOEdge` 12, including two trailing padding bytes. The map readers do not validate counts or offsets in these payloads.

[FilePNG.cpp](../MatrixLib/Bitmap/FilePNG.cpp) adapts in-memory PNG bytes through libpng. `ReadStart_Buf` borrows the input bytes and returns an opaque handle, dimensions and a format code: gray, RGB, RGBA or palette. Sixteen-bit samples are stripped to eight bits. `Read` consumes that handle, fills caller-provided rows/palette and releases reader state, including on a caught decode error. Keep the input alive until that call finishes and provide sufficient row stride/output storage. The wrapper's writer supports additional formats, so writer support alone does not establish reader support for each combination.

## Compatibility evidence and limits

Standalone sound uses a private UTF-16 `Sound` mapping from logical event suffixes to archived WAV paths. The production [WAV reader](../MatrixGame/src/Audio/Audio.cpp) accepts mono/stereo PCM8/16, checks RIFF/chunk/frame metadata and supports bounded legacy header/padding variations found in original clips. [Standalone sound](AUDIO.md) defines those variants, resource preparation and the separation between sound effects and unsupported music/Ogg playback. Synthetic audio cases exercise the reader without private clips.

Frozen checksum/storage bytes and synthetic tree/column/compression cases are reproducible without private packages. [layout_tests.cpp](../tests/layout_tests.cpp) builds the storage, `ZL03`, package and raw-record layouts above from explicit field widths, checks that production readers parse them and writers emit the same bytes, and covers the listed rejections. Its zlib fixtures use stored blocks, so they do not depend on a compressor version. Tracked text configuration can be exercised by the manual in-memory packing check. Live map loading/rendering requires the separately supplied game resources and the [standalone playtest](PLAYTESTING.md).

The existing readers validate some tags, types and lengths, but these checks do not establish rejection of every truncated, oversized or adversarial input; the unsafe cases above are not exercised by tests because they can read out of bounds or terminate the process. The engine suite has no complete PNG malformed-input coverage, map payload reader fixture or full map-schema fixture. Add bounded synthetic cases when changing those readers or their dependencies; preserve original packages unchanged and keep payloads out of commits and test artifacts.
