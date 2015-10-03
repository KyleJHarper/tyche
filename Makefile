# Hooray, a make file.

CC     = gcc -std=gnu99 -pthread -g
CFLAGS += -Wall

SRCDIR = src
OBJDIR = obj
BINDIR = bin

SOURCES  := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Targets
# The clock_gettime() on this gcc version requires -lrt.
build:
	$(CC) -o $(BINDIR)/hello $(SRCDIR)/hello.c
	$(CC) -o $(BINDIR)/sizes $(SRCDIR)/sizes.c
	$(CC) -o $(BINDIR)/tyche     \
		$(SRCDIR)/list.c    \
		$(SRCDIR)/lz4.c     \
		$(SRCDIR)/buffer.c  \
		$(SRCDIR)/lock.c    \
		$(SRCDIR)/error.c   \
		$(SRCDIR)/io.c      \
		$(SRCDIR)/tests.c   \
		$(SRCDIR)/main.c    \
		-lrt

clean:
	rm -f $(BINDIR)/*
	rm -f $(OBJECTS)
	echo "Cleanup complete!"
