CC= gcc
CFLAGS= -Og
LDLIBS = -lpthread
TARGET = myshell

objects = myshell.o

$(TARGET) : $(objects)
	$(CC) $(CFLAGS) -o $(TARGET) $(objects)

%.o : %.c
	$(CC) $(CFLAGS) -c -o $@ $<

myshell.o : myshell.h

.PHONY : clean
clean:
	rm $(TARGET) $(objects)
