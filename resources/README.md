The original application-icon JPEG and its PNG/ICO derivatives are kept together. Android uses the 512px PNG, Windows embeds the multi-resolution ICO and loads assets/app-icon.bmp for its window icon. The artwork retains the original game-asset rights described in LICENSE.txt.

`enhanced-title.png` is the supplied high-resolution Enhanced Edition title.
`enhanced-title-aof.png` is the reviewed alternate artwork, with a thin cyan
London Jolly 2 Special Edition subtitle based on the original AoF title.
The runtime `assets/enhanced-title*.tga` textures share the same base pixels
and transparent frame. The AoF texture adds the cyan subtitle inside that frame.
They load directly as native-resolution textures with bilinear sampling; source
sprite pages and the Original renderer remain unchanged. No fonts are bundled.

`enhanced-splash.jpg` is the supplied finished splash, including its lettering
and frame. Enhanced announcements are precolored, transparent 32-bit TGAs
at 6× their original sprite dimensions. The sprite IDs and UVs preserve
clipping, distortion and tile breakup. Got You! uses nearest sampling to
keep its pixel faces crisp. Original mode uses the source sprite sheets.

The supplied Cippy artwork is fitted to the 32 × 43 level selector slots at 6×.
The RGBA texture retains the colored glow, and the right arrow mirrors the left.
