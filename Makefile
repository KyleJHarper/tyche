# Hooray, a make file.

CC     = gcc
CFLAGS += -Wall -Wextra -std=gnu99 -pthread

SRCDIR       = src
LZ4_SRCS     = $(wildcard src/lz4/*.c)
ZLIB_SRCS    = $(wildcard src/zlib/*.c)
ZSTD_SRCS    = $(wildcard src/zstd/*.c)
OBJDIR       = obj
BINDIR       = bin
JEMALLOC_DIR = /usr/local/lib

SOURCES  := $(wildcard $(SRCDIR)/*.c)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Targets
# The clock_gettime() on this gcc version requires -lrt.
build:
	$(MAKE) examples
	$(MAKE) playground
	$(MAKE) hello
	$(MAKE) sizes
	$(MAKE) tyche

hello:
	$(CC) $(CFLAGS) -O3 -o $(BINDIR)/hello $(SRCDIR)/hello.c

sizes:
	$(CC) $(CFLAGS) -O3 -o $(BINDIR)/sizes $(SRCDIR)/sizes.c

tyche:
	$(CC) $(CFLAGS) -g -pg -Og -o $(BINDIR)/tyche_debug \
		$(LZ4_SRCS)                \
		$(ZLIB_SRCS)               \
		$(ZSTD_SRCS)               \
		$(SRCDIR)/list.c           \
		$(SRCDIR)/options.c        \
		$(SRCDIR)/buffer.c         \
		$(SRCDIR)/manager.c        \
		$(SRCDIR)/error.c          \
		$(SRCDIR)/io.c             \
		$(SRCDIR)/tests.c          \
		$(SRCDIR)/tyche.c          \
		-L$(JEMALLOC_DIR) -Wl,-rpath,${JEMALLOC_DIR}/ -ljemalloc -lrt -lm -g
	$(CC) $(CFLAGS) -O3 -o $(BINDIR)/tyche \
		$(LZ4_SRCS)                \
		$(ZLIB_SRCS)               \
		$(ZSTD_SRCS)               \
		$(SRCDIR)/list.c           \
		$(SRCDIR)/options.c        \
		$(SRCDIR)/buffer.c         \
		$(SRCDIR)/manager.c        \
		$(SRCDIR)/error.c          \
		$(SRCDIR)/io.c             \
		$(SRCDIR)/tests.c          \
		$(SRCDIR)/tyche.c          \
		-L$(JEMALLOC_DIR) -Wl,-rpath,${JEMALLOC_DIR}/ -ljemalloc -lrt -lm

examples:
	$(CC) $(CFLAGS) -O0 -o $(BINDIR)/example_simple \
		$(SRCDIR)/example_simple.c  \
		$(LZ4_SRCS)                 \
		$(ZLIB_SRCS)                \
		$(ZSTD_SRCS)                \
		$(SRCDIR)/list.c            \
		$(SRCDIR)/buffer.c          \
		$(SRCDIR)/error.c           \
		-L$(JEMALLOC_DIR) -Wl,-rpath,${JEMALLOC_DIR}/ -ljemalloc -lrt -lm

playground:
	$(CC) $(CFLAGS) -O0 -o $(BINDIR)/playground \
		$(SRCDIR)/playground.c

clean:
	rm -f $(BINDIR)/*
	rm -f $(OBJECTS)
	echo "Cleanup complete!"

