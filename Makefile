# Hooray, a make file.

CC=gcc
CFLAGS+=-Wall

SRCDIR = src
OBJDIR = obj
BINDIR = bin

SOURCES  := $(wildcard $(SRCDIR)/*.c)
INCLUDES := $(wildcard $(SRCDIR)/*.h)
OBJECTS  := $(SOURCES:$(SRCDIR)/%.c=$(OBJDIR)/%.o)

# Targets
build: $(SRCDIR)/hello.c
	$(CC) -o $(BINDIR)/hello $(SRCDIR)/hello.c


clean:
	rm -f $(BINDIR)/*
	rm -f $(OBJECTS)
	echo "Cleanup complete!"
