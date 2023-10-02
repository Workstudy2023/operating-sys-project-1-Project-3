CC	= gcc -g3
CFLAGS  = -g3 -Wall
TARGET1 = memparent
TARGET2 = memchild

OBJS1	= memparent.o
OBJS2	= memchild.o

all:	$(TARGET1) $(TARGET2)

$(TARGET1):	$(OBJS1)
	$(CC) -o $(TARGET1) $(OBJS1)

$(TARGET2):	$(OBJS2)
	$(CC) -o $(TARGET2) $(OBJS2)

memparent.o:	memparent.c
	$(CC) $(CFLAGS) -c memparent.c

memchild.o:	memchild.c
	$(CC) $(CFLAGS) -c memchild.c

clean:
	/bin/rm -f *.o $(TARGET1) $(TARGET2)
