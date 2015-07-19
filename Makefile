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
build:
	$(CC) -o $(BINDIR)/hello $(SRCDIR)/hello.c
	$(CC) -o $(BINDIR)/sizes $(SRCDIR)/sizes.c
	$(CC) -o $(BINDIR)/main     \
		$(SRCDIR)/buffer.c  \
		$(SRCDIR)/lock.c    \
		$(SRCDIR)/error.c   \
		$(SRCDIR)/list.c    \
		$(SRCDIR)/tests.c   \
		$(SRCDIR)/main.c

clean:
	rm -f $(BINDIR)/*
	rm -f $(OBJECTS)
	echo "Cleanup complete!"
