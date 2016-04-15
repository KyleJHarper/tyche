# Hooray, a make file.

CC     = gcc
CFLAGS += -Wall -Wextra -std=gnu99 -pthread

SRCDIR = src
OBJDIR = obj
BINDIR = bin
JEMALLOC_DIR = /usr/local/lib

SOURCES  := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Targets
# The clock_gettime() on this gcc version requires -lrt.
build:
	$(CC) $(CFLAGS) -g -pg -O0 -o $(BINDIR)/tyche_debug \
		$(SRCDIR)/lz4/lz4.c        \
		$(SRCDIR)/zlib/adler32.c   \
		$(SRCDIR)/zlib/compress.c  \
		$(SRCDIR)/zlib/crc32.c     \
		$(SRCDIR)/zlib/deflate.c   \
		$(SRCDIR)/zlib/gzclose.c   \
		$(SRCDIR)/zlib/gzlib.c     \
		$(SRCDIR)/zlib/gzread.c    \
		$(SRCDIR)/zlib/gzwrite.c   \
		$(SRCDIR)/zlib/infback.c   \
		$(SRCDIR)/zlib/inffast.c   \
		$(SRCDIR)/zlib/inflate.c   \
		$(SRCDIR)/zlib/inftrees.c  \
		$(SRCDIR)/zlib/trees.c     \
		$(SRCDIR)/zlib/uncompr.c   \
		$(SRCDIR)/zlib/zutil.c     \
		$(SRCDIR)/list.c           \
		$(SRCDIR)/options.c        \
		$(SRCDIR)/buffer.c         \
		$(SRCDIR)/manager.c        \
		$(SRCDIR)/error.c          \
		$(SRCDIR)/io.c             \
		$(SRCDIR)/tests.c          \
		$(SRCDIR)/tyche.c          \
		-L$(JEMALLOC_DIR) -Wl,-rpath,${JEMALLOC_DIR}/ -ljemalloc -lrt -lm
	$(CC) $(CFLAGS) -O3 -o $(BINDIR)/hello $(SRCDIR)/hello.c
	$(CC) $(CFLAGS) -O3 -o $(BINDIR)/playground $(SRCDIR)/playground.c
	$(CC) $(CFLAGS) -O3 -o $(BINDIR)/sizes $(SRCDIR)/sizes.c
	$(CC) $(CFLAGS) -O3 -o $(BINDIR)/tyche \
		$(SRCDIR)/lz4/lz4.c        \
		$(SRCDIR)/zlib/adler32.c   \
		$(SRCDIR)/zlib/compress.c  \
		$(SRCDIR)/zlib/crc32.c     \
		$(SRCDIR)/zlib/deflate.c   \
		$(SRCDIR)/zlib/gzclose.c   \
		$(SRCDIR)/zlib/gzlib.c     \
		$(SRCDIR)/zlib/gzread.c    \
		$(SRCDIR)/zlib/gzwrite.c   \
		$(SRCDIR)/zlib/infback.c   \
		$(SRCDIR)/zlib/inffast.c   \
		$(SRCDIR)/zlib/inflate.c   \
		$(SRCDIR)/zlib/inftrees.c  \
		$(SRCDIR)/zlib/trees.c     \
		$(SRCDIR)/zlib/uncompr.c   \
		$(SRCDIR)/zlib/zutil.c     \
		$(SRCDIR)/list.c           \
		$(SRCDIR)/options.c        \
		$(SRCDIR)/buffer.c         \
		$(SRCDIR)/manager.c        \
		$(SRCDIR)/error.c          \
		$(SRCDIR)/io.c             \
		$(SRCDIR)/tests.c          \
		$(SRCDIR)/tyche.c          \
		-L$(JEMALLOC_DIR) -Wl,-rpath,${JEMALLOC_DIR}/ -ljemalloc -lrt -lm

clean:
	rm -f $(BINDIR)/*
	rm -f $(OBJECTS)
	echo "Cleanup complete!"
