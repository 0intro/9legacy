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

#ifndef INT64_MAX
#define INT64_MAX  0x7FFFFFFFFFFFFFFFLL
#endif

typedef struct Block Block;
typedef struct File_index File_index;
typedef struct Member Member;

struct Block {
	vlong pos, size;			/* pos + size <= INT64_MAX */
};

static void
init_block(Block *b, vlong p, vlong s)
{
	b->pos = p;
	b->size = s;
}

static vlong
block_end(Block b)
{
	return b.pos + b.size;
}

struct Member {
	struct Block dblock, mblock;		/* data block, member block */
	unsigned dict_size;
};

static void
init_member(Member *m, vlong dp, vlong ds, vlong mp, vlong ms, unsigned dict_size)
{
	init_block(&m->dblock, dp, ds);
	init_block(&m->mblock, mp, ms);
	m->dict_size = dict_size;
}

struct File_index {
	struct Member *member_vector;
	char	*error;
	vlong isize;
	long	members;
	int	error_size;
	int	retval;
};

bool	Fi_init(File_index *fi, int infd, bool ignore_trailing);
void	Fi_free(File_index *fi);

static vlong
Fi_udata_size(File_index *fi)
{
	if (fi->members <= 0)
		return 0;
	return block_end(fi->member_vector[fi->members-1].dblock);
}

static vlong
Fi_cdata_size(File_index *fi)
{
	if (fi->members <= 0)
		return 0;
	return block_end(fi->member_vector[fi->members-1].mblock);
}

/* total size including trailing data (if any) */
static vlong
Fi_file_size(File_index *fi)
{
	if (fi->isize >= 0)
		return fi->isize;
	else
		return 0;
}

static struct Block *
Fi_dblock(File_index *fi, long i)
{
	return & fi->member_vector[i].dblock;
}

static struct Block *
Fi_mblock(File_index *fi, long i)
{
	return & fi->member_vector[i].mblock;
}

static unsigned
Fi_dict_size(File_index *fi, long i)
{
	return fi->member_vector[i].dict_size;
}
