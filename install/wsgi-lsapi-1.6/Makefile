CC=		x86_64-linux-gnu-gcc -pthread
BASECFLAGS=	-Wno-unused-result -Wsign-compare -DNDEBUG -g -fwrapv -O2 -Wall -g   -fstack-protector-strong -Wformat -Werror=format-security  -g -fwrapv -O2    -O0 -g3
CPPFLAGS=	-I. -I/usr/include/python3.8
DEFINES=	
CFLAGS=		$(BASECFLAGS) $(CPPFLAGS) $(DEFINES)
LIBPL=		/usr/lib/python3.8/config-3.8-x86_64-linux-gnu
LDFLAGS=	-Wl,-Bsymbolic-functions  -Wl,-z,relro -g -fwrapv -O2    -L/usr/lib
LINKFORSHARED=	-Xlinker -export-dynamic -Wl,-O1 -Wl,-Bsymbolic-functions
LIBS=		-lcrypt -lpthread -ldl  -lutil -lm
SYSLIBS=	-lm
VERSION=	3.8

OBJS=		main.o wsgi.o wsgiinput.o wsgiwrapper.o lsapilib.o lsapi-glue.o

all: lswsgi

lswsgi: $(OBJS)
	$(CC) $(LDFLAGS) $(LINKFORSHARED) -o $@ $(OBJS) -L$(LIBPL) -lpython$(VERSION) $(LIBS) $(SYSLIBS)

main.o: main.c  wsgi.h version.h

lsapilib.o: lsapilib.c lsapilib.h

wsgi.o: wsgi.c wsgi-int.h wsgi.h

wsgiinput.o: wsgiinput.c wsgi-int.h wsgi.h

wsgiwrapper.o: wsgiwrapper.c wsgi-int.h wsgi.h

lsapi-glue.o: lsapi-glue.c 

clean:
	rm -f lswsgi *.o core *.core

distclean: clean
	rm -f Python.framework Makefile
