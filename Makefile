# Hooray, a make file.

CC     = gcc -std=gnu99
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
	$(CC) -o $(BINDIR)/main      \
		$(SRCDIR)/lock.c         \
		$(SRCDIR)/error.c        \
		$(SRCDIR)/buffer_lists.c \
		$(SRCDIR)/main.c

clean:
	rm -f $(BINDIR)/*
	rm -f $(OBJECTS)
	echo "Cleanup complete!"
