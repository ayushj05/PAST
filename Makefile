CC = g++
CFLAGS = -std=c++17 -lpthread

SRCDIR = src
OBJDIR = obj
BINDIR = bin

output: $(OBJDIR)/PAST.o $(OBJDIR)/Pastry.o
	$(CC) $(OBJDIR)/PAST.o $(OBJDIR)/Pastry.o -o $(BINDIR)/PAST $(CFLAGS)

$(OBJDIR)/PAST.o: $(SRCDIR)/PAST.cpp
	$(CC) -c $(SRCDIR)/PAST.cpp $(CFLAGS) -o $(OBJDIR)/PAST.o

$(OBJDIR)/Pastry.o: $(SRCDIR)/Pastry.cpp $(SRCDIR)/Pastry.h
	$(CC) -c $(SRCDIR)/Pastry.cpp $(CFLAGS) -o $(OBJDIR)/Pastry.o

clean:
	rm -f $(OBJDIR)/* $(BINDIR)/PAST
