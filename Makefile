HEADERS = eprintf.h array.h httpcode.h respond.h
SOURCES = eprintf.c array.c httpcode.c respond.c main.c

SRCHEADERS = $(addprefix src/,${HEADERS})
SRCSOURCES = $(addprefix src/,${SOURCES})

main: ${SRCHEADERS} ${SRCSOURCES}
	cc -Wall -Wextra -g3 -O2 -fsanitize=address -o main ${SRCSOURCES}

clear:
	rm main
.PHONY: clear
