TARGET = harborlights
OBJS = src/main.o src/sfx.o src/app.o src/scene.o src/model.o src/font.o src/fonts.o src/gfx.o

INCDIR = src
CFLAGS = -O2 -G0 -Wall -Wno-unused-function -Wno-format-truncation -Wno-stringop-truncation
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =
LDFLAGS =
LIBS = -lpspaudio -lpsppower -lpsprtc -lm

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = Harbor Lights
PSP_EBOOT_ICON = assets/ICON0.PNG
PSP_EBOOT_PIC1 = assets/PIC1.PNG

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
