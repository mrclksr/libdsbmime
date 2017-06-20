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
#include <ctype.h>
#include <limits.h>
#include <sys/types.h>
#ifdef __linux__
# include <arpa/inet.h>
#endif
#include <err.h>
#include <stdbool.h>

#define MAGICSTR "MIME-Magic\0\n"

typedef struct magic_section_header_s {
	char	*mime_type;
	u_short prio;
} magic_section_header_t;

/*
 * Struct to represent a magic section record.
 */
typedef struct magic_section_record_s {
	int	rangelen;
	int	offset;
	char	wsize;
	char	indent;
	u_char	*val;
	u_char	*mask;
	u_short vlen;
	struct magic_section_record_s *next;
} magic_section_record_t;

/*
 * Struct to represent a magic file section.
 */
typedef struct magic_section_s {
	magic_section_header_t *hdr;
	magic_section_record_t *rec;
	struct magic_section_s *next;
} magic_section_t;

typedef struct magic_record_s {
#define MAGIC_TYPE_RECORD 0x1
#define MAGIC_TYPE_HEADER 0x2
	char type;
	union {
		magic_section_header_t shdr;
		magic_section_record_t srec;
	} rec;
} magic_record_t;

extern uint16_t htons(uint16_t);

static int	       buflen = 0, init = 0;
static u_char	       *buf = NULL;		/* General purpose buffer. */
static magic_section_t *magic_sections;

static u_char *
extend_buffer(size_t n)
{
	u_char *p;

	if (n < buflen)
		return (buf);
	if ((p = realloc(buf, n + 64)) == NULL)
		return (NULL);
	buf = p; buflen = n + 64;
	return (p);
}

static void
free_buffer(void)
{
	if (buf != NULL && buflen > 0)
		free(buf);
	buf = NULL; buflen = 0;
}

static bool
magic_match_record(FILE *fp, magic_section_record_t *rec)
{
	int  cc, c, n, i, j, rl, mask;

	for (; rec != NULL; rec = rec->next) {
		if (fseek(fp, rec->offset, SEEK_SET) == -1)
			return (false);
		if (buflen < rec->vlen)
			if (extend_buffer(rec->vlen) == NULL)
				return (false);
		rl = rec->rangelen;
		for (cc = n = 0; rl > 0 && n < rec->vlen;) {
			buf[cc++] = (u_char)(c = fgetc(fp));
			mask = rec->mask != NULL ? rec->mask[n] : 0xff;
			if ((u_char)(c & mask) != (rec->val[n] & mask)) {
				for (i = 1, j = 0; j + i < cc;) {
					mask = rec->mask != NULL ? \
					    rec->mask[j] : 0xff;
					if ((buf[i + j] & mask) !=
					    (rec->val[j] & mask))
						i++, j = 0;
					else
						j++;
				}	
				rl -= i;
				for (j = 0; i < cc; i++, j++)
					buf[j] = buf[i];
				n = cc = j;
			} else
				n++;
		}
		if (n != rec->vlen) {
			/* Not found. */
			if (rec->next == NULL ||
			    rec->next->indent > rec->indent)
				return (false);
		} else if (rec->next == NULL ||
		    rec->next->indent <= rec->indent)
			return (true);
	}
	return (false);
}

static magic_section_record_t *
magic_dup_record(magic_section_record_t *rec)
{
	magic_section_record_t *p;
	
	if ((p = malloc(sizeof(magic_section_record_t))) == NULL)
		return (NULL);
	(void)memcpy(p, rec, sizeof(magic_section_record_t));
	if ((p->val = malloc(rec->vlen)) == NULL) {
		free(p); return (NULL);
	}
	(void)memcpy(p->val, rec->val, rec->vlen);
	p->next = NULL;

	return (p);
}

static magic_section_t *
magic_new_section(void)
{
	magic_section_t *sec;

	if ((sec = malloc(sizeof(magic_section_t))) == NULL)
		return (NULL);
	sec->hdr  = NULL;
	sec->rec  = NULL;
	sec->next = NULL;

	return (sec);
}

static void
magic_free_sections(magic_section_t *sec)
{
	magic_section_t	       *next_sec;
	magic_section_record_t *srec, *next_srec;

	for (; sec != NULL; sec = next_sec) {
		next_sec = sec->next;
		if (sec->hdr != NULL) {
			if (sec->hdr->mime_type != NULL)
				free(sec->hdr->mime_type);
			free(sec->hdr);
		}
		for (srec = sec->rec; srec != NULL; srec = next_srec) {
			next_srec = srec->next;
			if (srec->val != NULL)
				free(srec->val);
			free(srec);
		}
	}
}

static magic_record_t *
magic_read_record(FILE *fp)
{
	int c, n;
	static char	      num[12];
	static magic_record_t  rec;
	magic_section_header_t *shdr;
	magic_section_record_t *srec;

	while ((c = fgetc(fp)) == '\n')
		;
	if (c == '[') {
		/* Header. A new section begins. */
		rec.type = MAGIC_TYPE_HEADER;
		shdr = &rec.rec.shdr;

		for (n = 0; n < sizeof(num) && (c = fgetc(fp)) != EOF &&
		    c != ':'; n++)
			num[n] = (char)c;
		num[n] = '\0';
		if (c != ':')
			/* Syntax error. */
			return (NULL);
		shdr->prio = (u_short)strtol(num, NULL, 10);
		for (n = 0; (c = fgetc(fp)) != EOF && c != ']'; n++) {
			if (n >= buflen)
				if (extend_buffer(n) == NULL)
					return (NULL);
			buf[n] = (char)c;
		}
		buf[n] = '\0';
		if (fgetc(fp) != '\n' || c != ']')
			/* Syntax error. */
			return (NULL);
		shdr->mime_type = (char *)buf;
		return (&rec);
	} else if (isdigit(c) || c == '>') {
		/* Section record. */
		rec.type = MAGIC_TYPE_RECORD;
		srec = &rec.rec.srec;

		/* Set default values. */
		srec->mask     = NULL;
		srec->wsize    = 1;
		srec->indent   = 0;
		srec->rangelen = 1;

		if (isdigit(c)) {
			/* Indent. */
			n = 0;
			num[n++] = (char)c;
			for (; n < sizeof(num) && (c = fgetc(fp)) != EOF &&
			    c != '>'; n++)
				num[n] = (char)c;
			num[n] = '\0';
			if (c != '>')
				/* Syntax error. */
				return (NULL);
			srec->indent = (char)strtol(num, NULL, 10);
		} else if (c != '>')
			return (NULL);
		/* Get the start-offset. */
		for (n = 0;
		    n < sizeof(num) && (c = fgetc(fp)) != EOF && c != '='; n++)
			num[n] = (char)c;
		num[n] = '\0';
		if (c != '=')
			return (NULL);
		srec->offset = (int)strtol(num, NULL, 10);
		
		/* Get the value length and the value. */
		for (n = 0; n < 2 && (c = fgetc(fp)) != EOF; n++)
			num[n] = (char)c;
		if (n != 2)
			return (NULL);
		srec->vlen = htons(*((u_short *)num));
		/* Read the value. */
		if (extend_buffer(srec->vlen) == NULL)
			return (NULL);
		for (n = 0; n < srec->vlen && (c = fgetc(fp)) != EOF; n++)
			buf[n] = (char)c;
		srec->val = buf;
		if (c == EOF)
			return (NULL);
		
		while ((c = fgetc(fp)) != EOF)
			switch (c) {
			case '\n':
				return (&rec);
			case '&':
				for (n = 0; n < 2 &&
				    (c = fgetc(fp)) != EOF; n++)
					num[n] = (char)c;
				if (n != 2)
					return (NULL);
				if (buflen < srec->vlen * 2 &&
				    extend_buffer(srec->vlen * 2) == NULL)
					return (NULL);
				srec->mask = buf + srec->vlen;
				for (n = 0; n < srec->vlen &&
				    (c = fgetc(fp)) != EOF; n++)
					srec->mask[n] = (u_char)c;
				if (n != srec->vlen)
					return (NULL);
				break;
			case '~':
			case '+':
				/* FALLTHROUGH */
				for (n = 0; n < sizeof(num) &&
				    (c = fgetc(fp)) != EOF && isdigit(c); n++)
					num[n] = (char)c;
				num[n] = '\0';
				if (n == 0 || n > sizeof(num))
					return (NULL);
				if (c == '~')
					srec->wsize =
					    (char)strtol(num, NULL, 10);
				else
					srec->rangelen =
					    (char)strtol(num, NULL, 10);
				(void)ungetc(c, fp);
				break;
			default:
				/* Ignore line. Seek to next line. */
				while ((c = fgetc(fp)) != '\n' && c != EOF)
					;
				return (NULL);
			}
	}
	return (NULL);
}

static magic_section_t *
magic_read_file(const char *path)
{
	FILE *fp;
	magic_record_t	       *rec;
	magic_section_t	       *sec, *magic;
	magic_section_record_t *srec;

	if ((fp = fopen(path, "r")) == NULL)
		return (NULL);
	if (extend_buffer(sizeof(MAGICSTR)) == NULL) {
		free_buffer();
		return (NULL);
	}
	if (fgets((char *)buf, sizeof(MAGICSTR), fp) == NULL) {
		(void)fclose(fp); free_buffer();
		return (NULL);
	}
	if (memcmp(buf, MAGICSTR, sizeof(MAGICSTR) - 1) != 0) {
		warnx("%s: %s doesn't seem to be a valid magic file", LIBNAME,
		    path);
		(void)fclose(fp); free_buffer();
		return (NULL);
	}
	sec = magic = NULL;
	while (!feof(fp)) {
		rec = magic_read_record(fp);
		if (rec == NULL)
			continue;
		if (rec->type == MAGIC_TYPE_HEADER) {
			if (magic != NULL) {
				if ((sec->next = magic_new_section()) == NULL)
					return (NULL);
				sec = sec->next;
			} else {
				if ((magic = magic_new_section()) == NULL)
					return (NULL);
				sec = magic;
			}
			sec->hdr = malloc(sizeof(magic_section_record_t));
			if (sec->hdr == NULL) {
				(void)fclose(fp); return (NULL);
			}
			sec->hdr->prio = rec->rec.shdr.prio;
			sec->hdr->mime_type = strdup(rec->rec.shdr.mime_type);
			if (sec->hdr->mime_type == NULL) {
				(void)fclose(fp); free(sec->hdr);
				return (NULL);
			}
		} else {
			if (sec->rec == NULL) {
				/* First section record in section. */
				sec->rec = magic_dup_record(&rec->rec.srec);
				srec = sec->rec;
			} else {
				/* Add a new section record to section. */
				srec->next = magic_dup_record(&rec->rec.srec);
				srec = srec->next;
			}
		}
	}
	(void)fclose(fp);
	return (magic);
}

const char *
magic_lookup_mime_type(const char *file)
{
	FILE		*fp;
	magic_section_t *mp;

	if ((fp = fopen(file, "r")) == NULL) {
		warn("%s: fopen(%s)", LIBNAME, file); return (NULL);
	}
	for (mp = magic_sections; mp != NULL; mp = mp->next)
		if (magic_match_record(fp, mp->rec)) {
			(void)fclose(fp);
			return (mp->hdr->mime_type);
		}
	(void)fclose(fp);
	return (NULL);
}

int
magic_init(const char *magicpath)
{
	if (init != 0)
		return (-1);
	buflen = 0; buf = NULL;
	if ((magic_sections = magic_read_file(magicpath)) == NULL)
		return (-1);
	init = 1; 
	return (0);
}

void
magic_cleanup(void)
{
	if (init == 0)
		return;
	if (magic_sections != NULL)
		magic_free_sections(magic_sections);
	free_buffer();
	init = 0;
}

