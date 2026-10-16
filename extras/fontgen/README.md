# fontgen - bitmap fonts for P4Panel32

`make_vn_font.py` turns a TrueType font into C++ headers that `P4Panel32::setFont()` can use.
The ready-made `src/DejaVuVN_9.h`, `DejaVuVN_12.h` and `DejaVuVN_16.h` were made with the default command.

```
pip install -r requirements.txt
python make_vn_font.py                                   # sizes 9 12 16, DejaVu Sans -> ../../src
python make_vn_font.py --sizes 10 14                     # other sizes -> src/DejaVuVN_10.h, DejaVuVN_14.h
python make_vn_font.py --sizes 16 --ttf DejaVuSans-Bold.ttf --name DejaVuBoldVN
python make_vn_font.py --sizes 12 --preview preview.png --text "Xin chào Việt Nam" --ascii
```

| Option | Meaning |
|---|---|
| `--sizes N ...` | em size in pixels (default `9 12 16`). The real cell height is larger, see below |
| `--ttf FILE` | any TrueType font that has the Vietnamese letters (default: DejaVu Sans, found via matplotlib or the system fonts) |
| `--name PREFIX` | C++ name prefix, the file/symbol is `<PREFIX>_<size>` (default `DejaVuVN`) |
| `--out DIR` | output folder (default `../../src`) |
| `--mode mono\|gray` | `mono` (default): FreeType 1-bit hinted rendering, the crispest. `gray`: light-hinted anti-aliased, then thresholded (`--threshold`); bolder at 12/16 px but the marks of the 9 px font get lost |
| `--autohint` | use the FreeType auto-hinter instead of the font's own hints |
| `--extra "←→"` | add characters |
| `--preview F.png`, `--text`, `--ascii` | magnified preview / ASCII art of a sample text, to judge a size before flashing |

The script prints, for every size, `ascent`, `descent` and `height`: the line cell has to hold the stacked marks of the tallest
capitals (Ấ, Ẫ ...) and the descenders, so it is taller than the em size (9 px -> 13 rows, 12 -> 17, 16 -> 21).
Ordinary text needs fewer rows; see the table in the main README.

Character set: ASCII 32..126, the 134 Vietnamese letters (67 lower + 67 upper), and `° ± × ÷ · – — ‘ ’ “ ” … • ₫ € £ ©`.
Combining marks (NFD text) are not included; use precomposed letters.

Licence: DejaVu fonts - see `LICENSE_DEJAVU` and `../../NOTICE`. If you generate a font from another TrueType file, check its licence.
