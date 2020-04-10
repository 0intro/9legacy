/*  Clzip - LZMA lossless data compressor
    Copyright (C) 2010-2017 Antonio Diaz Diaz.

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <stdbool.h>

#include "lzip.h"
#include "file_index.h"

static int
seek_read(int fd, uint8_t *buf, int size, vlong pos)
{
	if (lseek(fd, pos, SEEK_SET) == pos)
		return readblock(fd, buf, size);
	return 0;
}

static bool
add_error(File_index *fi, char *msg)
{
	int len = strlen(msg);
	void * tmp = resize_buffer(fi->error, fi->error_size + len + 1);
	if (!tmp)
		return false;
	fi->error = (char *)tmp;
	strncpy(fi->error + fi->error_size, msg, len + 1);
	fi->error_size += len;
	return true;
}

static bool
push_back_member(File_index *fi, vlong dp, vlong ds, vlong mp, vlong ms, unsigned dict_size)
{
	Member *p;
	void * tmp = resize_buffer(fi->member_vector,
	    (fi->members + 1) * sizeof fi->member_vector[0]);
	if (!tmp) {
		add_error( fi, "Not enough memory." );
		fi->retval = 1;
		return false;
	}
	fi->member_vector = (Member *)tmp;
	p = &(fi->member_vector[fi->members]);
	init_member(p, dp, ds, mp, ms, dict_size);
	++fi->members;
	return true;
}

static void
Fi_free_member_vector(File_index *fi)
{
	if (fi->member_vector) {
		free(fi->member_vector);
		fi->member_vector = 0;
	}
	fi->members = 0;
}

static void
Fi_reverse_member_vector(File_index *fi)
{
	Member tmp;
	long	i;
	for (i = 0; i < fi->members / 2; ++i) {
		tmp = fi->member_vector[i];
		fi->member_vector[i] = fi->member_vector[fi->members-i-1];
		fi->member_vector[fi->members-i-1] = tmp;
	}
}

static void
Fi_set_errno_error(File_index *fi, char *msg)
{
	add_error(fi, msg);
	add_error(fi, strerror(errno));
	fi->retval = 1;
}

static void
Fi_set_num_error(File_index *fi, char *msg, uvlong num)
{
	char	buf[80];
	snprintf( buf, sizeof buf, "%s%llu", msg, num );
	add_error(fi, buf);
	fi->retval = 2;
}

/* If successful, push last member and set pos to member header. */
static bool
Fi_skip_trailing_data(File_index *fi, int fd, vlong *pos)
{
	enum {
		block_size = 16384,
		buffer_size = block_size + Ft_size -1 + Fh_size
	};
	uint8_t buffer[buffer_size];
	int	bsize = *pos % block_size;	/* total bytes in buffer */
	int	search_size, rd_size;
	uvlong ipos;
	int	i;

	if (bsize <= buffer_size - block_size)
		bsize += block_size;
	search_size = bsize;			/* bytes to search for trailer */
	rd_size = bsize;			/* bytes to read from file */
	ipos = *pos - rd_size;			/* aligned to block_size */
	if (*pos < min_member_size)
		return false;

	while (true) {
		uint8_t max_msb = (ipos + search_size) >> 56;
		if (seek_read(fd, buffer, rd_size, ipos) != rd_size) {
			Fi_set_errno_error( fi, "Error seeking member trailer: " );
			return false;
		}
		for (i = search_size; i >= Ft_size; --i)
			if (buffer[i-1] <= max_msb)	/* most significant byte of member_size */ {
				File_header header;
				File_trailer * trailer = (File_trailer *)(buffer + i - Ft_size);
				uvlong member_size = Ft_get_member_size(*trailer);
				unsigned	dict_size;
				if (member_size == 0) {
					while (i > Ft_size && buffer[i-9] == 0)
						--i;
					continue;
				}
				if (member_size < min_member_size || member_size > ipos + i)
					continue;
				if (seek_read(fd, header, Fh_size,
				    ipos + i - member_size) != Fh_size) {
					Fi_set_errno_error( fi, "Error reading member header: " );
					return false;
				}
				dict_size = Fh_get_dict_size(header);
				if (!Fh_verify_magic(header) || !Fh_verify_version(header) ||
				    !isvalid_ds(dict_size))
					continue;
				if (Fh_verify_prefix(buffer + i, bsize - i)) {
					add_error( fi, "Last member in input file is truncated or corrupt." );
					fi->retval = 2;
					return false;
				}
				*pos = ipos + i - member_size;
				return push_back_member(fi, 0, Ft_get_data_size(*trailer), *pos,
				    member_size, dict_size);
			}
		if (ipos <= 0) {
			Fi_set_num_error( fi, "Member size in trailer is corrupt at pos ",
			    *pos - 8);
			return false;
		}
		bsize = buffer_size;
		search_size = bsize - Fh_size;
		rd_size = block_size;
		ipos -= rd_size;
		memcpy(buffer + rd_size, buffer, buffer_size - rd_size);
	}
}

bool
Fi_init(File_index *fi, int infd, bool ignore_trailing)
{
	File_header header;
	vlong pos;
	long i;

	fi->member_vector = 0;
	fi->error = 0;
	fi->isize = lseek(infd, 0, SEEK_END);
	fi->members = 0;
	fi->error_size = 0;
	fi->retval = 0;
	if (fi->isize < 0) {
		Fi_set_errno_error( fi, "Input file is not seekable: " );
		return false;
	}
	if (fi->isize < min_member_size) {
		add_error( fi, "Input file is too short." );
		fi->retval = 2;
		return false;
	}
	if ((uvlong)fi->isize > INT64_MAX) {
		add_error( fi, "Input file is too long (2^63 bytes or more)." );
		fi->retval = 2;
		return false;
	}

	if (seek_read(infd, header, Fh_size, 0) != Fh_size) {
		Fi_set_errno_error( fi, "Error reading member header: " );
		return false;
	}
	if (!Fh_verify_magic(header)) {
		add_error(fi, bad_magic_msg);
		fi->retval = 2;
		return false;
	}
	if (!Fh_verify_version(header)) {
		add_error(fi, bad_version(Fh_version(header)));
		fi->retval = 2;
		return false;
	}
	if (!isvalid_ds(Fh_get_dict_size(header))) {
		add_error(fi, bad_dict_msg);
		fi->retval = 2;
		return false;
	}

	pos = fi->isize;	/* always points to a header or to EOF */
	while (pos >= min_member_size) {
		File_trailer trailer;
		uvlong member_size;
		unsigned dict_size;

		if (seek_read(infd, trailer, Ft_size, pos - Ft_size) != Ft_size) {
			Fi_set_errno_error( fi, "Error reading member trailer: " );
			break;
		}
		member_size = Ft_get_member_size(trailer);
		if (member_size < min_member_size || member_size > (uvlong)pos) {
			if (fi->members > 0)
				Fi_set_num_error( fi, "Member size in trailer is corrupt at pos ",
				    pos - 8);
			else if (Fi_skip_trailing_data(fi, infd, &pos)) {
				if (ignore_trailing)
					continue;
				add_error(fi, trailing_msg);
				fi->retval = 2;
				return false;
			}
			break;
		}
		if (seek_read(infd, header, Fh_size, pos - member_size) != Fh_size) {
			Fi_set_errno_error( fi, "Error reading member header: " );
			break;
		}
		dict_size = Fh_get_dict_size(header);
		if (!Fh_verify_magic(header) || !Fh_verify_version(header) ||
		    !isvalid_ds(dict_size)) {
			if (fi->members > 0)
				Fi_set_num_error( fi, "Bad header at pos ", pos - member_size );
			else if (Fi_skip_trailing_data(fi, infd, &pos)) {
				if (ignore_trailing)
					continue;
				add_error(fi, trailing_msg);
				fi->retval = 2;
				return false;
			}
			break;
		}
		pos -= member_size;
		if (!push_back_member(fi, 0, Ft_get_data_size(trailer), pos,
		    member_size, dict_size))
			return false;
	}
	if (pos != 0 || fi->members <= 0) {
		Fi_free_member_vector(fi);
		if (fi->retval == 0) {
			add_error( fi, "Can't create file index." );
			fi->retval = 2;
		}
		return false;
	}
	Fi_reverse_member_vector(fi);
	for (i = 0; i < fi->members - 1; ++i) {
		vlong end = block_end(fi->member_vector[i].dblock);
		if (end < 0 || (uvlong)end > INT64_MAX) {
			Fi_free_member_vector(fi);
			add_error( fi, "Data in input file is too long (2^63 bytes or more)." );
			fi->retval = 2;
			return false;
		}
		fi->member_vector[i+1].dblock.pos = end;
	}
	return true;
}

void
Fi_free(File_index *fi)
{
	Fi_free_member_vector(fi);
	if (fi->error) {
		free(fi->error);
		fi->error = 0;
	}
	fi->error_size = 0;
}
