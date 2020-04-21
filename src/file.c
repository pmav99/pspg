/*-------------------------------------------------------------------------
 *
 * file.c
 *	  a routines related to file processing
 *
 * Portions Copyright (c) 2017-2020 Pavel Stehule
 *
 * IDENTIFICATION
 *	  src/file.c
 *
 *-------------------------------------------------------------------------
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "pspg.h"

/*
 * Replace tilde by HOME dir
 */
char *
tilde(char *dest, char *path)
{
	static char buffer[MAXPATHLEN];

	int			chars = 0;
	char	   *w;

	if (!dest)
		dest = buffer;

	w = dest;

	while (*path && chars < MAXPATHLEN - 1)
	{
		if (*path == '~')
		{
			char *home = getenv("HOME");

			if (home == NULL)
				leave("HOME directory is not defined");

			while (*home && chars < MAXPATHLEN - 1)
			{
				*w++ = *home++;
				chars += 1;
			}
			path++;
		}
		else
		{
			*w++ = *path++;
			chars += 1;
		}
	}

	*w = '\0';

	return dest;
}

/*
 * Deduce format type from file suffix
 */
static int
get_format_type(char *path)
{
	char		buffer[4];
	char	   *r_ptr, *w_ptr;
	int			i;
	int			l;

	l = strlen(path);
	if (l < 5)
		return FILE_MATRIX;

	r_ptr = path + l - 4;
	w_ptr = buffer;

	if (*r_ptr++ != '.')
		return FILE_MATRIX;

	for (i = 0; i < 3; i++)
		*w_ptr++ = tolower(*r_ptr++);

	*w_ptr = '\0';

	if (strcmp(buffer, "csv") == 0)
		return FILE_CSV;
	else if (strcmp(buffer, "tsv") == 0)
		return FILE_TSV;
	else
		return FILE_MATRIX;
}

/*
 * Try to open input stream.
 */
bool
open_data_file(Options *opts, StateData *state, bool reopen)
{
	state->_errno = 0;
	state->errstr = NULL;

	if (opts->pathname)
	{
		char	   *pathname = tilde(state->pathname, opts->pathname);

		errno = 0;

		state->fp = fopen(pathname, "r");
		if (!state->fp)
		{
			/* save errno, and prepare error message */
			state->_errno = errno;
			format_error("cannot to open file \"%s\" (%s)", pathname, strerror(errno));
			return false;
		}

		state->file_format_from_suffix = get_format_type(opts->pathname);
	}
	else
	{
		/* there is not a path name */
		state->pathname[0] = '\0';

		/* use stdin as input if query cannot be used as source */
		if (!opts->query)
		{
			state->fp = stdin;
			state->is_pipe = true;
		}
	}

	if (state->fp)
	{
		struct stat statbuf;

		if (fstat(fileno(state->fp), &statbuf) != 0)
		{
			state->_errno = errno;
			format_error("cannot to get status of file \"%s\" (%s)", state->pathname, strerror(errno));
			return false;
		}

		state->is_fifo = S_ISFIFO(statbuf.st_mode);		/* is FIFO file or pipe */
		state->is_file = S_ISREG(statbuf.st_mode);		/* is regular file */

		/*
		 * when source is FIFO and not pipe, then we can protect source
		 * against POLLHUP sugnal. One possibility how to do it is reopening
		 * stream with write acess. Then POLLHUP signal is never raised.
		 */
		if (state->is_fifo && !state->is_pipe && state->hold_stream == 2)
			freopen(NULL, "a+", state->fp);

		if (state->stream_mode)
		{
			/* ensure non blocking read from pipe or fifo */
			if (state->is_file)
			{
				if (!state->has_notify_support)
					leave("streaming on file is not available without file notification service");

				state->detect_truncation = true;
				fseek(state->fp, 0L, SEEK_END);
				state->last_position = ftell(state->fp);
			}
			else
			{
				/* in stream mode we use non block reading for FIFO or pipes */
				fcntl(fileno(state->fp), F_SETFL, O_NONBLOCK);
			}
		}

		state->is_blocking = !(fcntl(fileno(state->fp), F_GETFL) & O_NONBLOCK);
	}

	return true;
}
