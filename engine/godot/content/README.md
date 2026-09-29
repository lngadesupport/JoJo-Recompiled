# Local imported content

This directory is intentionally empty in Git.

Build and run `jojo_content_importer` against a legally obtained user copy
of the original game:

```text
jojo_content_importer "JoJo's Bizarre Adventure (USA).cue" engine/godot/content
```

The importer writes:

- `manifest.json`
- `raw/P/*.PAC` — graphics/content packs
- `raw/M/*.BIN` — characters, hitboxes, UI and script data
- `raw/X/*.XA` — source audio streams
- `raw/C/*.CLT|*.FIN` — palettes/color metadata
- `derived/palettes/*.tga` — modern palette previews
- `derived/text/*.txt` — candidate text extracted from data files

It never copies `SLUS_010.60`, `SYSTEM.CNF` or `ZNULL.DAT` into the
native engine content tree.
