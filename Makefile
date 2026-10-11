# hemisphere - build on the Pi with: make
CC      ?= gcc
CFLAGS  ?= -O2 -Wall -Wextra
CFLAGS  += $(shell sdl2-config --cflags)
LDLIBS  += $(shell sdl2-config --libs) -lGLESv2 -lm

SRC = main.c dome/dome.c dome/text.c scenes/deathstar.c scenes/planet.c scenes/plasma.c \
      scenes/julia.c scenes/kaleido.c scenes/life.c \
      scenes/moon.c scenes/orbit.c scenes/groovy.c \
      scenes/scifi.c
OBJ = $(SRC:.c=.o)

hemisphere: $(OBJ)
	$(CC) -o $@ $(OBJ) $(LDLIBS)

dome/dome.o: dome/dome.c dome/dome.h dome/shaders.h
dome/text.o: dome/text.c dome/dome.h dome/font5x7.h
main.o scenes/deathstar.o scenes/plasma.o scenes/julia.o \
scenes/kaleido.o scenes/life.o scenes/moon.o scenes/orbit.o \
scenes/groovy.o scenes/scifi.o: dome/dome.h
scenes/planet.o: scenes/planet.c dome/dome.h third_party/stb_image.h
	$(CC) $(CFLAGS) -Wno-unused-function -c -o $@ $<

clean:
	rm -f hemisphere $(OBJ)

.PHONY: clean
