HEADERS = eprintf.h array.h httpcode.h magicfile.h respond.h
SOURCES = eprintf.c array.c httpcode.c magicfile.c respond.c main.c

SRCHEADERS = $(addprefix src/,${HEADERS})
SRCSOURCES = $(addprefix src/,${SOURCES})

main: ${SRCHEADERS} ${SRCSOURCES}
	cc -Wall -Wextra -g3 -O2 -fsanitize=address -o main -lmagic ${SRCSOURCES}

clear:
	rm main
.PHONY: clear
