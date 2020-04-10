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

#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <unistd.h>
#include <sys/stat.h>

#include "lzip.h"
#include "file_index.h"

static void
list_line(uvlong uncomp_size, uvlong comp_size, char *input_filename)
{
	if (uncomp_size > 0)
		printf( "%15llu %15llu %6.2f%%  %s\n", uncomp_size, comp_size,
		    100.0 * (1.0 - ((double)comp_size / uncomp_size)),
		    input_filename);
	else
		printf( "%15llu %15llu   -INF%%  %s\n", uncomp_size, comp_size,
		    input_filename);
}

int
list_files(char *filenames[], int num_filenames, bool ignore_trailing)
{
	uvlong	total_comp = 0, total_uncomp = 0;
	int	files = 0, retval = 0;
	int	i;
	bool first_post = true;
	bool stdin_used = false;
	for (i = 0; i < num_filenames; ++i) {
		char * input_filename;
		File_index file_index;
		struct stat in_stats;				/* not used */
		int	infd;
		bool from_stdin = ( strcmp( filenames[i], "-" ) == 0 );
		if (from_stdin) {
			if (stdin_used)
				continue;
			else
				stdin_used = true;
		}
		input_filename = from_stdin ? "(stdin)" : filenames[i];
		infd = from_stdin ? STDIN_FILENO :
		    open_instream(input_filename, &in_stats, true, true);
		if (infd < 0) {
			if (retval < 1)
				retval = 1;
			continue;
		}

		Fi_init(&file_index, infd, ignore_trailing);
		close(infd);
		if (file_index.retval != 0) {
			show_file_error(input_filename, file_index.error, 0);
			if (retval < file_index.retval)
				retval = file_index.retval;
			Fi_free(&file_index);
			continue;
		}
		if (verbosity >= 0) {
			uvlong udata_size = Fi_udata_size(&file_index);
			uvlong cdata_size = Fi_cdata_size(&file_index);

			total_comp += cdata_size;
			total_uncomp += udata_size;
			++files;
			if (first_post) {
				first_post = false;
				if (verbosity >= 1)
					fputs( "   dict   memb  trail ", stdout );
				fputs( "   uncompressed      compressed   saved  name\n", stdout );
			}
			if (verbosity >= 1) {
				vlong trailing_size;
				unsigned dict_size = 0, fidictsz;
				long i;

				for (i = 0; i < file_index.members; ++i) {
					fidictsz = Fi_dict_size(&file_index, i);
					if (fidictsz > dict_size)
						dict_size = fidictsz;
				}
				trailing_size = Fi_file_size(&file_index) - cdata_size;
				printf( "%s %5ld %6lld ", format_ds( dict_size ),
				    file_index.members, trailing_size);
			}
			list_line(udata_size, cdata_size, input_filename);

			if (verbosity >= 2 && file_index.members > 1) {
				long	i;
				fputs(" member      data_pos       data_size      member_pos     member_size\n", stdout);
				for (i = 0; i < file_index.members; ++i) {
					Block * db = Fi_dblock(&file_index, i);
					Block * mb = Fi_mblock(&file_index, i);
					printf( "%5ld %15llu %15llu %15llu %15llu\n",
					    i + 1, db->pos, db->size, mb->pos, mb->size);
				}
				first_post = true;	/* reprint heading after list of members */
			}
			fflush(stdout);
		}
		Fi_free(&file_index);
	}
	if (verbosity >= 0 && files > 1) {
		if (verbosity >= 1)
			fputs("                      ", stdout);
		list_line( total_uncomp, total_comp, "(totals)" );
		fflush(stdout);
	}
	return retval;
}
