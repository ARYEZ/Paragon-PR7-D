# THE KIT VIEW's pictures (0.14.0)

Our own 3D drum kit, modelled and rendered for PR7-D in the PR7 family's colours, built into the plugin as
`KitImages::` binary data (see `CMakeLists.txt`) and drawn by `source/ui/KitView.cpp`.

| File | What |
|---|---|
| `kit.jpg` | The kit from the drummer's seat, 1280 x 924 |
| `kitzones.png` | The click map: each pixel's value (all channels) is the slot a click there plays + 1 (0: nothing) |
| `lit<piece>.png` | Each piece glowing brighter, cut to the area it changes; its alpha fades where the glow fades |
| `kitlayout.txt` | `<piece> <x> <y> <width> <height>`: where each glow goes on `kit.jpg` |

## Making them again

Needs Blender 4.0 (`apt install blender`) and Python 3 with numpy and Pillow. In an empty folder with these scripts:

```bash
mkdir -p raw
blender -b -P kit3d.py -- $PWD/raw/mask.png 1 1280 924 panel mask
blender -b -P kit3d.py -- $PWD/raw/base.png 160 1280 924 panel tron
python3 regions.py raw/mask.png > raw/regions.txt
while read piece region; do blender -b -P kit3d.py -- $PWD/raw/lit-$piece.png 160 1280 924 panel lit:$piece $region; done < raw/regions.txt
python3 process_assets.py raw <the repo>/resources/kit
```

- `kit3d.py`: the scene. Arguments: output, samples, width, height, view (`panel` is the plugin's; `kit`, `front`,
  `logo`, `cymbal` and `pedal` were for the mock-ups), pass (`tron`: the kit; `mask`: the click map, each part a flat
  colour; `lit:<piece>`: that piece's glow about 6 times brighter) and, for a lit pass, the region to render (`x0,y0,x1,y1`).
- `regions.py`: each piece's region (its parts on the click map, with room for its halo and its reflection).
- `process_assets.py`: every hue into the family's range (330-30 degrees: the render's tone mapping burns the
  brightest orange towards yellow and the bloom turns pink towards purple), the click map decoded from the sRGB curve,
  each glow cut out with its alpha, and the layout. JPEG still moves a few colours a few degrees (and rings beside
  bright glints), so `KitView` pulls every hue into 318-42 degrees once more as it loads the pictures.

The base render takes about 8 minutes on 4 CPU cores; the glows about a minute each.

Which part plays which slot is in `kit3d.py`'s `zone_of` and `KitView.cpp`'s `slotPieces`: keep them together.
