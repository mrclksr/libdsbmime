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
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <errno.h>
#include <err.h>
#include <fnmatch.h>
#include "glob.h"

#define M 27		/* A good constant for the hash function. */

typedef struct glob_s {
	bool	      hashed;
	char	      *glob;
	char	      *mime_type;	
	struct glob_s *next;
} glob_t;

typedef struct hash_s {
	glob_t	      *glob;
	struct hash_s *next;
} hash_t;

static int    hashsize = 0, init = 0;
static hash_t **hashtbl = NULL;
static glob_t *globlst = NULL;

/*
 * Get the first prime number >= n.
 */
static int
get_nearest_prime(int n)
{
	int p, z, d, ok, sqrt_z;
	
	if (n <= 1)
		return (2);
	else if (n <= 3)
		return (3);
	for (p = z = 3, sqrt_z = 1; p < n;) {
		z += 2; sqrt_z++;
		if (sqrt_z * sqrt_z > z)
			sqrt_z--;
		for (ok = 1, d = 3; ok == 1 && d <= sqrt_z; d += 2)
			if (z % d == 0)
				ok = 0;
		if (ok == 1)
			p = z;
	}
	return (p);
}

static int
glob_hash_string(const char *str, bool igncase)
{
	static int h;

	for (h = 0; *str != '\0';) {
		h *= M;
		if (igncase)
			h += ((unsigned char)tolower(*str++) % M);
		else
			h += ((unsigned char)*str++ % M);
		h %= hashsize;
	}
	return (h);
}

static glob_t *
glob_read_file(const char *path)
{
	FILE   *fp;
	char   *buf, *glob, *mime;
	glob_t *gp;

	if ((fp = fopen(path, "r")) == NULL)
		return (NULL);
	if ((buf = malloc(_POSIX2_LINE_MAX)) == NULL) {
		fclose(fp); return (NULL);
	}
	while (fgets(buf, _POSIX2_LINE_MAX, fp) != NULL) {
		if (buf[0] == '#' || !isdigit(buf[0]))
			continue;
		(void)strtok(buf, "\n");
		if ((mime = strchr(buf, ':')) == NULL)
			continue;
		mime++;
		if ((glob = strchr(mime, ':')) == NULL)
			continue;
		*glob++ = '\0';
		if (globlst == NULL) {
			if ((globlst = malloc(sizeof(glob_t))) == NULL) {
				fclose(fp); free(buf); return (NULL);
			}
			gp = globlst;
			gp->next = NULL;
		} else if ((gp->next = malloc(sizeof(glob_t))) == NULL) {
			fclose(fp); free(buf); return (NULL);
		} else
			gp = gp->next;
		if ((gp->mime_type = strdup(mime)) == NULL ||
		    (gp->glob = strdup(glob)) == NULL) {
			fclose(fp); free(buf); return (NULL);
		}
		gp->next = NULL;
		hashsize++;
	}
	fclose(fp); free(buf);

	return (globlst);
}

static void
glob_free_list(void)
{
	glob_t *gp, *next_gp;

	for (gp = globlst; gp != NULL; gp = next_gp) {
		next_gp = gp->next;
		free(gp->glob);
		free(gp->mime_type);
		free(gp);
	}
	globlst = NULL;
}

static hash_t **
glob_gen_hashtbl(void)
{
	int    hash, i;
	char   *p, *q;
	glob_t *gp;
	hash_t *hp;

	hashsize = get_nearest_prime(hashsize);
	if ((hashtbl = malloc(sizeof(hash_t *) * hashsize)) == NULL)
		return (NULL);
	for (i = 0; i < hashsize; i++)
		hashtbl[i] = NULL;
	for (gp = globlst; gp != NULL; gp = gp->next) {
		if (gp->glob[0] != '*' || gp->glob[1] != '.')
			continue;
		for (q = p = gp->glob + 2; *p != '\0'; p++)
			if (strchr("*?[", *p) != NULL)
				break;
		if (*p != '\0') {
			gp->hashed = false;
			continue;
		}
		hash = glob_hash_string(q, false);
		gp->hashed = true;

		if (hashtbl[hash] == NULL) {
			hashtbl[hash] = malloc(sizeof(hash_t));
			hashtbl[hash]->next = NULL;
			hashtbl[hash]->glob = gp;
		} else {
			for (hp = hashtbl[hash]; hp->next != NULL;
			    hp = hp->next)
				;
			hp->next = malloc(sizeof(hash_t));
			hp = hp->next;
			hp->next = NULL;
			hp->glob = gp;
		}
	}
	return (hashtbl);
}

static void
glob_free_hashtbl(void)
{
	int    i;
	hash_t *hp, *next_hp;

	if (hashtbl == NULL)
		return;
	for (i = 0; i < hashsize; i++)
		for (hp = hashtbl[i]; hp != NULL; hp = next_hp) {
			next_hp = hp->next; free(hp);
		}
	free(hashtbl); hashtbl = NULL;
}

int
glob_init(const char *globpath)
{
	if (init != 0)
		return (-1);
	globlst = NULL; hashtbl = NULL; hashsize = 0;

	if (glob_read_file(globpath) == NULL) {
		if (errno != 0)
			warn("glob_read_file(%s)", globpath);
		glob_free_list();
		return (-1);
	}
	if (glob_gen_hashtbl() == NULL) {
		if (errno != 0)
			warn("glob_gen_hashtbl()");
		if (hashtbl != NULL)
			glob_free_hashtbl();
		return (-1);
	}
	init = 1;
	return (0);
}

void
glob_cleanup(void)
{
	if (init == 0)
		return;
	glob_free_hashtbl();
	glob_free_list();
	init = 0;
}

const char *
glob_lookup_mime_type(const char *filename, bool igncase)
{
	int	   hash, matches;
	glob_t	   *gp;
	hash_t	   *hp;
	const char *p;

	for (matches = 0, gp = NULL, p = filename;
	    (p = strchr(p, '.')) != NULL && matches == 0;) {
		hash = glob_hash_string(++p, igncase);
		for (hp = hashtbl[hash]; hp != NULL; hp = hp->next)
			/* Skip '*.' in hp->glob->glob. */
			if (!igncase && strcmp(hp->glob->glob + 2, p) == 0) {
				/* Try a case sensitive match. */
				gp = hp->glob; matches++;
			} else if (strcasecmp(hp->glob->glob + 2, p) == 0) {
				gp = hp->glob; matches++;
			}
	}	
	if (matches > 1)
		/* Match is ambiguous. */
		return (NULL);
	else if (matches == 0) {
		/* No match - Try to find mime type by using fnmatch(). */
		for (gp = globlst; gp != NULL; gp = gp->next)
			if (!gp->hashed)
				if (!fnmatch(gp->glob, filename, FNM_NOESCAPE))
					return (gp->mime_type);
		return (NULL);
	}
	/* Unique match. */
	return (gp->mime_type);
}

