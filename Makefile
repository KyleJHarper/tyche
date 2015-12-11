# Hooray, a make file.

CC     = gcc
CFLAGS += -Wall -std=gnu99 -pthread -g
#CFLAGS += -Wall -std=gnu99 -pthread -O3

SRCDIR = src
OBJDIR = obj
BINDIR = bin

SOURCES  := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Targets
# The clock_gettime() on this gcc version requires -lrt.
build:
	$(CC) $(CFLAGS) -o $(BINDIR)/hello $(SRCDIR)/hello.c
	$(CC) $(CFLAGS) -o $(BINDIR)/sizes $(SRCDIR)/sizes.c
	$(CC) $(CFLAGS) -o $(BINDIR)/tyche     \
		$(SRCDIR)/list.c    \
		$(SRCDIR)/lz4.c     \
		$(SRCDIR)/options.c \
		$(SRCDIR)/buffer.c  \
		$(SRCDIR)/manager.c \
		$(SRCDIR)/lock.c    \
		$(SRCDIR)/error.c   \
		$(SRCDIR)/io.c      \
		$(SRCDIR)/tests.c   \
		$(SRCDIR)/tyche.c   \
		-lrt

clean:
	rm -f $(BINDIR)/*
	rm -f $(OBJECTS)
	echo "Cleanup complete!"
