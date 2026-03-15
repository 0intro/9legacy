typedef long long vlong;

extern int __SEEK(vlong*, int, vlong, int);

vlong
_SEEK(int fd, vlong o, int p)
{
	vlong l;

	if(__SEEK(&l, fd, o, p) < 0)
		l = -1LL;
	return l;
}
