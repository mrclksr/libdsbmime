
# NAME

**libdsbmime** - Library to determine file types

# SYNOPSIS

**#include &lt;dsbmime.h>**

*int*  
**dsbmime\_init**(*void*);

*char \*&zwnj;*  
**dsbmime\_get\_type**(*const char \*file*);

*void*  
**dsbmime\_cleanup**(*void*);

# DESCRIPTION

**libdsbmime**
is a C library to identify a file's MIME type by using
freedesktop.org's Shared MIME database package.

Before using any other function of the library,
**dsbmime\_init**()
must be called. The function
**dsbmime\_get\_type**()
returns the MIME type of the given
*file*
as a string stored in a static buffer. Subsequent calls
to the same function will modify that buffer. In order to free memory used
by the library, the function
**dsbmime\_cleanup**()
can be called.

# RETURN VALUES

**dsbmime\_init**()
returns -1 if an error has occurred, else 0.
**dsbmime\_get\_type**()
returns a pointer to a string containing the
*file*'s
MIME type, or
`NULL`
if the file type could not be determined. If
an error has occurred,
`NULL`
is returned and
*errno*
is set.

# INSTALLATION

	# make install

or if you're not happy with the predefined PREFIX:

	# make PREFIX=/somewhere/else install

## DEPENDENCIES

**libdsbmime**
needs freedsktop.org's Shared MIME database package.

# INSTALLED FILES

*${PREFIX}/include/dsbmime.h*

> Include file

*${PREFIX}/lib/libdsbmime.a*

> Static library file

*${PREFIX}/man/man3/libdsbmime.3.gz*

> Manunal page

# EXAMPLES

See
*test.c*

