DMDFonts folder 

The .dmd files in this folder contain font definitions for 128x32 DMD
(dot matrix display) use.  These files use the .dmd animation format
defined by PyProcGame, another open-source program.  See
pyprocgame.pindev.org for details on the file format.

We use these fonts to generate custom DMD text displays at run-time,
mostly for the purposes of showing live high score information on the
DMD.

Note that we don't use the .dmd files at run-time, so they don't need
to be included with the binary distribution.  They're in the source
tree because we use them during the build process to generate an
internal C++ representation.

The following .dmd files included in this folder are from
pinballmakers.com (see http://pinballmakers.com/wiki/index.php/Fonts),
which makes its materials available for free use with no restrictions.

Font_CC_12px_az.dmd
Font_CC_20px_az.dmd
Font_CC_9px_az.dmd
Font_CC_7px_az.dmd
Font_CC_5px_AX.dmd

The files above are described as re-creations of the fonts used in
Cactus Canyon.  We use them since they represent a fairly generic set
of DMD fonts similar to those used in most WPC era games, which makes
them harmonize well when mixed with original DMD graphics.
