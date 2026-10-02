# dino-stalker-unpacker

Extracts and converts the data of **Dino Stalker** (PlayStation 2, Capcom, 2002) into common formats:

| Game data | Converted to |
|---|---|
| Images (`P2IG`) | PNG |
| Models (`P2OD`): enemies, items, weapons, levels | glTF 2.0 with embedded textures |
| Motions (`P2MT`) | glTF animations on the matching models |
| Packs (`DATA/PACK/*.PAK`) | decompressed `.bin` |
| `SOUND.BIN` | WAV samples, plus the raw sound banks and music sequences |
| Movies (`MOVIE*/*.PSS`) | MPEG-2 video (`.m2v`) and WAV audio |

It reads the disc image directly, so you do not need to extract the files first. Tested with the European release (SLES-50930, version 1.02).

## Use

Drag the disc image, a folder or individual files onto `dino-stalker-unpacker.exe` (its window stays open until you press Enter), or run it from a command prompt:

```
dino-stalker-unpacker.exe "Dino Stalker (Europe).iso"
dino-stalker-unpacker.exe --output-dir=Extracted --no-movies "Dino Stalker (Europe).iso"
dino-stalker-unpacker.exe DATA\ENEMYDT\MD\TREXMD.BIN
```

A disc image is extracted to `<name> (extracted)` next to it:

- `disc\`: every file on the disc, unchanged
- `converted\`: the converted files, in the disc's folder layout. For example, `converted\DATA\ENEMYDT\MD\TREXMD\` holds `TREXMD.gltf` (the T-Rex with its texture and 66 animations), `images\000_T_REX.png` and `contents.txt`.

A folder is converted to `<folder>_extracted` next to it, keeping its layout, and a single file to a `<name>_<extension>` folder next to it. Converting a folder again skips the tool's own earlier output.

| Option | |
|---|---|
| `--output-dir=PATH` | Write everything under PATH |
| `--no-animations` | Export models without animations |
| `--no-movies` | Skip the movies (about 2.4 GB of output) |
| `--no-disc-files` | With a disc image, do not copy the unchanged files |
| `--fps=N` | Animation frames per second (default 60) |
| `--scale=N` | Scale models by N (default: the size the game draws them at, in metres; `--scale=1` keeps the units of the files) |

### What you get

- **Images**: `images\NNN_NAME.png`, in file order, with the name the game gives each image. Every converted file also gets `contents.txt`, a list of the blocks in it (offset, size, type, name, dimensions), including data the tool does not convert.
- **Enemies** (`DATA\ENEMYDT\MD`, `NOTDINO`, `STGENEDT`): rigged glTF, one bone per body part, with the motions from `DATA\ENEMYDT\PMT` (matched by bone count and name: `TREXMD` gets `TREXPMT`). Each motion is an animation named after its file and index (`TREXPMT_03`). In Blender: File > Import > glTF 2.0, then pick the action. Blender's default Solid view shows only plain colours; switch the viewport to Material Preview (Z, then 2) to see the textures.
- **Levels** (`DATA\WORLD\ST*_BIN.BIN`): one glTF per model, textured as in the game. The ground and scenery models are in level coordinates, so importing all of a level's files rebuilds the level. `ST2/3/7_GND.BIN` are copies of the start of the matching `ST*_BIN.BIN` and are not converted twice.
- **Items and weapons** (`DATA\ITEM\ITM_BIN.BIN`, `DATA\BLT.BIN`), insects, shadows and the 2D artwork (gallery, menus, title, results, fonts).
- **Sound** (`SOUND.BIN`):
  - `vab_NN\`: sound-effect banks. `bank.vab` plus one WAV per sample.
  - `sequence_NN.sq` and `instruments_NN\` (`bank.hd`, `bank.bd`, one WAV per sample): the music, in Sony's PS2 sound-library format. The music is sequenced, so it is not a finished recording; [VGMTrans](https://github.com/vgmtrans/vgmtrans) can play the `.sq`/`.hd`/`.bd` files and convert them to MIDI and SoundFont.
  - `unindexed_*\`: sample data that no bank describes (the game plays it by position). Split at the ADPCM end markers and saved at an assumed 22050 Hz.
- **Movies**: `.m2v` (MPEG-2 video; plays in VLC, mpv or ffmpeg) and `.wav` (48 kHz stereo). `MOVIE` holds the 60 Hz versions and `MOVIE50` the 50 Hz ones.

## Known limitations

- **Level object placement**: breakable and repeated objects (crates, cars, trees) are placed in the level by tables the tool does not decode yet, so they export at their own origin.
- **Texture binding outside the European release**: level, item, bullet and insect models do not say which images they use; the game's set-up code binds them. `src/TextureBindings.cpp` holds those bindings for the European version 1.02. With other versions, files of a different size fall back to the image order, which is likely to be wrong for levels.
- **Collision copies**: models the game never binds to textures (such as the collision copies of breakable objects) export untextured.
- **Units and sizes**: models import in metres at the size the game draws them. Enemies are made in millimetres and the game scales each type by its own factor, which the files do not hold: the red raptor (`RPTR`) is drawn 1.4 times its model size, the small ones (`RPTOB`) 0.85 times, the T-Rex 1.5 times (about 25 m long). `src/EnemySizes.cpp` holds these factors, taken from the game's code. Ordinary enemies also get a random 0-0.135 added in game, so sizes vary slightly; the export uses the base size. `PRSOMD` is also used 1.6 times larger for a second enemy type. Other models made in millimetres (items, insects, shadows) are recognised by size (over 500 units across) and converted to metres. Use `--scale=1` for the units of the files.
- **Not converted**: the title water video (`TTLWATER.IPU`), the memory-card icon, camera paths (`KMD`), stage event data (`SETDATA`), demo data and the credits (`STAFF.BIN`). They are in `disc\`.
- **Sound**: one VAB bank in `SOUND.BIN` (`vab_09`) has only its header there; its samples are elsewhere. VAB sample rates are worked out from each sample's tuning and may be off for samples the game plays at another pitch.
- **Animation timing**: motions are keyed in frames and exported at 60 frames per second; the PAL game may run them at 50.

## File formats

All values are little-endian. Most files are back-to-back `P2IG`/`P2OD`/`P2MT` blocks with no index; offsets inside a block are from the block start, and blocks start on 16-byte boundaries. The source files in `src/` describe each format in more detail.

### P2IG: image

| Offset | |
|---|---|
| 0x00 | `"P2IG"`, u32 0x61, u32 0, u32 type (bit 3: swizzled) |
| 0x10 | char name[8], 8 bytes |
| 0x20 | u16 log2 width, u16 log2 height, u32 GS pixel format |
| 0x40 | u32 palette offset, u32 palette size, u32 pixel offset, u32 pixel size |

Pixel formats: 0x00 RGBA32, 0x01 RGB24, 0x02 RGBA16, 0x13 8-bit and 0x14 4-bit indexed; bits 8+ give the palette format (0 = RGBA32, 2 = RGBA16). Pixels are linear, except when bit 3 of the type is set (the enemy textures and two effects): then they are swizzled into GS memory order, so the game can upload an 8-bit image as 32-bit data at half the width and height (4-bit: PSMCT32 read back as PSMT4). 8-bit palettes use the GS CSM1 order (entries 8-15 and 16-23 of every 32 are swapped). Alpha 0x80 is opaque.

### P2OD: model

- Header: `"P2OD"`, u32 0x80, u32 part count; part records (0x50 bytes each) from 0x20: s16 parent, s16 child, s16 sibling, u16, u32 mesh offset, u32, then a 4x4 float matrix (row vectors, translation in the last row; -Y is up).
- Mesh: u32 group count, u32 flags (bit 0: skinned), 8 bytes, bounding box min/max.
- Group (0x60 bytes): u32 strip count, u32 attributes (bit 0 normals, bit 1 colours, bit 2 UVs), s32 texture index (local to the model, -1 = none), u32, float[4], four RGBA float colours.
- Strip: u32 vertex count, u32 GS primitive bits, 8 bytes, then 16-byte arrays: positions, normals, colours, UVs. Position w: bit 15 = draw the triangle ending at this vertex; in skinned meshes the low bits give the part whose space the vertex is in (its own, or its parent's at the joints). Colours are 0-255 floats with 128 as full brightness.

### P2MT: motion

- Header: `"P2MT"`, u32 0x7000, u32 bone count; bone records (0x50 bytes each) from 0x10: u32 key offset, u32 key count, u32 channels (bit 0 rotation, bit 2 translation, bit 3 scale), u32, rest matrix.
- Key: u32 frames until the next key (0 on the last), 12 bytes, then for each channel present: quaternion (w, x, y, z), translation, scale. The quaternion is applied to row vectors, so the standard rotation is its conjugate. A translation key is added to the rest translation.

### PAK

u32 decompressed size, then LZSS: a flag byte per 8 items (LSB first; 1 = literal byte), and for a 0 bit a 2-byte reference with length `(b0 & 0x1F) + 3` and distance `(b0 >> 5) | (b1 << 3)`. Each pack is a stage's files back to back; all of them also exist on the disc on their own.

### PSS: movie

An MPEG-2 program stream with 16 KB packs. Video is stream 0xE0. Audio is in private stream 1, sub-stream 0xFF (4-byte sub-stream header): an `"SShd"` header (format, rate, channels, interleave) and `"SSbd"` body of 16-bit PCM with the channels interleaved in 0x200-byte blocks.

## Building

Requires Qt 6 (Core and Gui) and CMake. On Windows with Qt's MinGW kit:

```
set CMAKE_PREFIX_PATH=C:\Qt\6.10.3\mingw_64
build-windows.cmd
```

Then run `windeployqt build\dino-stalker-unpacker.exe` to copy the Qt runtime next to it.

## Credits

Dino Stalker is © Capcom. This tool contains no game data; you need your own copy of the game.

## License

GPL-3.0. See [LICENSE](LICENSE) or https://www.gnu.org/licenses/gpl-3.0.html.
