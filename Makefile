# hemisphere - build with: make
#   on the Pi (or any Linux): OpenGL ES 2.0 from libGLESv2
#   on Windows, in an MSYS2 UCRT64 terminal: the same command, builds
#   hemisphere.exe (see "Windows PC" in the README)
#   cross-compiling for Windows from Linux:
#     make WIN=1 CC=x86_64-w64-mingw32-gcc SDL2_CONFIG="/path/to/sdl2-config --prefix=/path/to/sdl"
CC          ?= gcc
CFLAGS      ?= -O2 -Wall -Wextra
SDL2_CONFIG ?= sdl2-config

ifeq ($(OS),Windows_NT)
WIN = 1
endif

CFLAGS += $(shell $(SDL2_CONFIG) --cflags)

ifdef WIN
EXE     = hemisphere.exe
# a console program, so the messages show; tools/windows hides the window
LDLIBS += $(filter-out -mwindows,$(shell $(SDL2_CONFIG) --libs)) -mconsole -lws2_32 -lm
else
EXE     = hemisphere
LDLIBS += $(shell $(SDL2_CONFIG) --libs) -lGLESv2 -lm
endif

SRC = main.c dome/dome.c dome/glload.c dome/text.c \
      scenes/deathstar.c scenes/planet.c scenes/plasma.c \
      scenes/julia.c scenes/kaleido.c scenes/life.c \
      scenes/moon.c scenes/orbit.c scenes/groovy.c \
      scenes/scifi.c scenes/computers.c \
      scenes/hypno.c scenes/balls.c scenes/retro.c scenes/screen.c \
      scenes/weather.c
OBJ = $(SRC:.c=.o)

$(EXE): $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)
ifdef WIN
	@# the exe needs SDL2.dll beside it; take it from the SDL2 install
	@test -f SDL2.dll || cp "$$($(SDL2_CONFIG) --prefix)/bin/SDL2.dll" . || \
	    echo "Copy SDL2.dll next to hemisphere.exe"
endif

HDR = dome/dome.h dome/glload.h
dome/dome.o: dome/dome.c $(HDR) dome/shaders.h
dome/glload.o: dome/glload.c dome/glload.h
dome/text.o: dome/text.c $(HDR) dome/font5x7.h
main.o scenes/deathstar.o scenes/plasma.o scenes/julia.o \
scenes/kaleido.o scenes/life.o scenes/moon.o scenes/orbit.o \
scenes/groovy.o scenes/scifi.o scenes/hypno.o \
scenes/balls.o scenes/retro.o: $(HDR)
scenes/computers.o: scenes/computers.c $(HDR) dome/font5x7.h
scenes/screen.o: scenes/screen.c $(HDR) dome/font5x7.h third_party/stb_image.h
scenes/planet.o: scenes/planet.c $(HDR) third_party/stb_image.h
scenes/weather.o: scenes/weather.c $(HDR) third_party/stb_image.h
	$(CC) $(CPPFLAGS) $(CFLAGS) -Wno-unused-function -c -o $@ $<

clean:
	rm -f hemisphere hemisphere.exe $(OBJ)

.PHONY: clean
