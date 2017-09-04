TARGET = ffmpeg-httpd
######################################################
CC = $(CROSSCOMPILER)gcc
CFLAGS = 
INCS = -I./ -I/usr/local/ffmpeg/include
LIBS = -L/usr/local/ffmpeg/lib -lavcodec -lavdevice -lavfilter -lavformat -lavutil -lpthread -lz -lm

all: $(TARGET)

SOURCES = server.c ffmpeg.c ffmpeg-httpd.c
OBJECTS = $(SOURCES:.c=.o)

$(TARGET) : $(OBJECTS)
	$(CC) -O2 -o $@ $(INCS) $(CFLAGS) $^ $(LIBS)

%.o:%.c
	$(CC) -O2 -c -o $@ $(INCS) $(CFLAGS) $^
clean:
	@rm -vrf $(TARGET) $(OBJECTS) 
	@rm -vrf *.o *~

