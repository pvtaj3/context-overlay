# Milestone D: layered renderer correctness

This isolated renderer slice makes two compositing rules explicit:

1. `UpdateLayeredWindow` with `AC_SRC_ALPHA` requires premultiplied BGRA. Every
   DIB pixel must have its RGB channels scaled by its alpha before presentation.
2. Outline-only GDI geometry must select `NULL_BRUSH`; otherwise the fill can
   overwrite the card background and alter the alpha mask.

`src/renderer_alpha.h` and its host test cover the premultiplication arithmetic
and profile-aware surface alpha policy. The Windows renderer integration should
apply `renderer::premultiply` across the final DIB before calling
`UpdateLayeredWindow`, and explicitly select/restore `NULL_BRUSH` around
outline-only drawing.

Runtime validation belongs on Windows with light/dark acrylic, opaque fallback,
rounded corners, and text over bright/dark backgrounds. This branch is kept
separate from the UIA and bridge work so compositing regressions are isolated.
