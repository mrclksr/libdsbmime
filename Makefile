LIBNAME	    = libdsbmime
PREFIX	   ?= /usr/local
MIMEPREFIX  = ${PREFIX}/share
MANDIR	    = ${PREFIX}/man/man3
LIBSDIR	    = ${PREFIX}/lib
INCSDIR	    = ${PREFIX}/include
MANPAGE	    = ${LIBNAME}.3
TARGET	    = ${LIBNAME}.a
HEADER	    = dsbmime.h
SOURCES	    = mime.c glob.c magic.c
OBJECTS	    = mime.o glob.o magic.o
CFLAGS	   += -Wall -DPATH_MIMEPREFIX=\"${MIMEPREFIX}\"
CFLAGS	   += -DLIBNAME=\"${LIBNAME}\"
TESTCFLAGS  = -Wall -ldsbmime -I${INCSDIR} -I. -L${LIBSDIR} -L.
BSD_INSTALL_DATA ?= install -m 0644

${TARGET}: ${OBJECTS}
	${AR} -cq ${TARGET} ${OBJECTS}

.SUFFIXES: .o
.c.o:
	${CC} ${CFLAGS} -c -o $@ $<

${OBJECTS}: ${SOURCES}

${MANPAGE}.gz: ${MANPAGE}
	gzip -k ${MANPAGE}

install: ${TARGET} ${MANPAGE}.gz
	${BSD_INSTALL_DATA} ${TARGET} ${DESTDIR}${LIBSDIR}
	${BSD_INSTALL_DATA} ${HEADER} ${DESTDIR}${INCSDIR}
	${BSD_INSTALL_DATA} ${MANPAGE}.gz ${DESTDIR}${MANDIR}

test: test.c
	${CC} -o $@ test.c ${TESTCFLAGS}

readme: readme.mdoc
	mandoc -mdoc readme.mdoc | perl -e 'foreach (<STDIN>) { \
		$$_ =~ s/(.)\x08\1/$$1/g; $$_ =~ s/_\x08(.)/$$1/g; print $$_ \
	}' | sed '1,1d' > README

readmemd: readme.mdoc
	mandoc -mdoc -Tmarkdown readme.mdoc | sed '1,1d; $$,$$d' > README.md

clean:
	-rm -f ${TARGET} ${OBJECTS} ${MANPAGE}.gz test

