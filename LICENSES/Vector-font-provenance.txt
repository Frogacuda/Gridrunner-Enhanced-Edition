# Vector font provenance

neon-hud is an independently authored 4x6 monoline alphabet. Additional ASCII
punctuation is independently drawn; lowercase input uses uppercase letterforms.

vectrex-menu reproduces the GCE Vectrex system character forms. References:
- Bruce Tomlin's published glyph table: https://www.playvectrex.com/designit/chrissalo/appendixe.htm
- Vectrex Llamatron visual reference: https://sadenby.com/games/llamatron/play.html

Each contiguous lit row run becomes a short horizontal vector segment. The
seven scan rows remain separate to preserve the system-character appearance.
Lowercase aliases uppercase; additional braces, vertical bar, tilde and grave
accent are independently drawn extensions. The documented arrow/circle forms
are also available.

font_hud.inc and font_menu.inc contain glyph definitions. Character-form
provenance is separate from the independently implemented parser, layout,
caching and rendering. No emulator implementation, BIOS/cartridge program,
ROM or external font library is distributed with the component. Gridrunner's
game/menu text uses its original bitmap assets rather than this vector-font API.
