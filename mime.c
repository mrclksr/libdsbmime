/*-
 * Copyright (c) 2016 Marcel Kaiser. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <sys/types.h>
#include <unistd.h>
#include <sys/stat.h>
#include <err.h>
#include <pwd.h>

#include "glob.h"
#include "magic.h"

static int init = 0;

int
dsbmime_init(void)
{
	int	      i, n;
	char	      *path, *base[2] = { NULL };
	bool	      have_globs, have_magic;
	struct stat   sb;
	struct passwd *pw;

	if (init != 0)
		return (-1);
	n = 0;
	have_globs = have_magic = false;

	if ((base[n++] = strdup(PATH_MIMEPREFIX)) == NULL) {
		if (base[0] != NULL)
			free(base[0]);
		return (-1);
	}
	base[n] = getenv("XDG_DATA_HOME");
	if (base[n] == NULL) {
		if ((pw = getpwuid(getuid())) == NULL) {
			if (errno != 0)
				warn("%s: getpwuid()", LIBNAME);
			else {
				warnx("%s: Couldn't find you in /etc/passwd.",
				    LIBNAME);
			}
			return (-1);
		}
		endpwent();
		base[n] = malloc(strlen(pw->pw_dir) +
		    strlen(".local/share") + 2);
		if (base[n] == NULL)
			return (-1);
		(void)sprintf(base[n++], "%s/.local/share", pw->pw_dir);
	} else {
		if ((base[n] = strdup(base[n])) == NULL)
			return (-1);
		n++;
	}
	for (i = 0; i < n && init < 2; i++) {
		path = malloc(strlen(base[i]) + strlen(PATH_GLOBS) + 2);
		if (path == NULL)
			return (-1);
		(void)sprintf(path, "%s/%s", base[i], PATH_GLOBS);
		if (stat(path, &sb) == -1) {
			if (errno != ENOENT)
				warn("stat(%s)", path);
		} else if (glob_init(path) == -1) {
			free(path); return (-1);
		} else {
			init++;
			have_globs = true;
		}
		free(path);
		path = malloc(strlen(base[i]) + strlen(PATH_MAGIC) + 2);
		if (path == NULL)
			return (-1);
		(void)sprintf(path, "%s/%s", base[i], PATH_MAGIC);
		if (stat(path, &sb) == -1) {
			if (errno != ENOENT)
				warn("%s: stat(%s)", LIBNAME, path);
		} else if (magic_init(path) == -1) {
			free(path); return (-1);
		} else {
			init++;
			have_magic = true;
		}
		free(path);
	}
	for (i = 0; i < n; i++)
		free(base[i]);
	if (!have_globs) {
		warnx("%s: Could not find globs file (%s)", LIBNAME,
		    PATH_GLOBS);
	}
	if (!have_magic) {
		warnx("%s: Could not find magic file (%s)", LIBNAME,
		    PATH_MAGIC);
	}
	if (init == 0)
		return (-1);
	return (0);
}

const char *
dsbmime_get_type(const char *filename)
{
	const char *mime;

	if (init == 0)
		return (NULL);
	if ((mime = glob_lookup_mime_type(filename, false)) == NULL &&
	    (mime = glob_lookup_mime_type(filename, true)) == NULL)
		mime = magic_lookup_mime_type(filename);
	return (mime);
}

void
dsbmime_cleanup(void)
{
	if (init == 0)
		return;
	magic_cleanup();
	glob_cleanup();
	init = 0;
}

