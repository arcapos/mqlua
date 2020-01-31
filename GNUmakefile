PROG=		mqlua
SRCS=		mqlua.c node.c zmq.c

LUAVER=		`lua -v 2>&1 | cut -c 5-7`

MANDIR=		/usr/share/man/man1

CFLAGS+=	-Wall -O3 -pthread -DHOUSEKEEPING \
		-I/usr/include -I/usr/include/lua${LUAVER}
LDADD+=		-llua${LUAVER} -lzmq

NOMAN=		1
MAN=		mqlua.1

BINDIR=		/usr/bin

.SUFFIXES= .cpp .c

OBJS=		${SRCS:.c=.o}
CPPOBJS=	${CPPSRCS:.cpp=.o}
OBJDIR?=	obj

XDIR?=		/usr
PKGDIR?=	/usr
BINDIR?=	/usr/bin
PGDIR=		/usr/include/postgresql
INSTFILES?=
FILEMODE?=	0644

LINK?=		cc

MANUAL=

ifneq ($(NOMAN), 1)
MANUAL=		$(MAN)
endif

LOCALEDIR=	/usr/share/locale

INSTALL?=	install

all:		${PROG}

clean:
		-rm -f *.o ${PROG} ${CLEANFILES}

${PROG}:	${OBJS} ${CPPOBJS}
		${LINK} ${CFLAGS} ${LDFLAGS} -o ${PROG} ${OBJS} ${CPPOBJS} \
			${LDADD}

.PHONY: install $(MANUAL) $(FILES) msginstall
install:	${PROG} $(MANUAL) msginstall
		${INSTALL} -d ${DESTDIR}${BINDIR}
		${INSTALL} -d ${DESTDIR}${FILESDIR}
		${INSTALL} -s -o root -g root ${PROG} $(DESTDIR)$(BINDIR)
		for FILE in ${INSTFILES}; \
		do \
			${INSTALL} -m ${FILEMODE} \
				$${FILE} ${DESTDIR}${FILESDIR}/$${FILE}; \
		done

$(MANUAL):
		${INSTALL} -d $(DESTDIR)${MANDIR}
		${INSTALL} -o root -g root -m 644 $@ $(DESTDIR)${MANDIR}/$@
		gzip -f $(DESTDIR)${MANDIR}/$@

obj:
		mkdir obj

.cpp.o:
		g++ -O3 -fPIC -c -o $@ ${CPPFLAGS} $<

.c.o:
		gcc -O3 -fPIC -c -o $@ ${CFLAGS} $<
