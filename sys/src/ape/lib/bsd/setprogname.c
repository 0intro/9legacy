extern char *argv0;

void
setprogname(const char *progname)
{
	argv0 = (char *)progname;
}
