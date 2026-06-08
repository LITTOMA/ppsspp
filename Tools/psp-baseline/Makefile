TARGET = ppsspp_3ds_baseline
OBJS = main.o

INCDIR =
CFLAGS = -Os -Wall -G0
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)

LIBDIR =
LDFLAGS =
LIBS =

EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PPSSPP 3DS Baseline

PSPSDK = $(shell psp-config --pspsdk-path)
include $(PSPSDK)/lib/build.mak
