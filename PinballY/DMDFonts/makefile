# Makefile for DMD fonts
# This is invoked as part of the Visual Studio build process

HEADERS = \
    Font_CC_5px_AZ.h \
    Font_CC_7px_az.h \
    Font_CC_9px_az.h \
    Font_CC_12px_az.h \
    Font_CC_15px_az.h \
    Font_CC_20px_az.h

all: $(HEADERS)

*.dmd: "$(OutDir)\DMDFontTool.exe"

.SUFFIXES: .dmd .h
.dmd.h:
    "$(OutDir)\DMDFontTool" "$<" "$(@B)" "$@"
