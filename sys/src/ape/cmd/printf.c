#include <u.h>
#include <libc.h>
#ifdef PLAN9PORT
#include <libString.h>
#else
#include <String.h>
#endif
#include <bio.h>

void	usage(void);
void	xprintf(char *, int, char **);
int	puto(Biobuf *, char *);
int	putx(Biobuf *, char *);
int	cton(char);
int	format(Biobuf *, char *, char **, char **);
int	result(String *, char *, char **, int);

void
main(int argc, char **argv)
{
	ARGBEGIN {
	default:
		usage();
	} ARGEND
	if(argc == 0)
		usage();
	xprintf(*argv, argc-1, argv+1);
	exits(nil);
}

void
usage(void)
{
	fprint(2, "usage: %s format [arg ...]\n", argv0);
	exits("usage");
}

void
xprintf(char *fmt, int narg, char **args)
{
	Biobuf b;
	char c, c1;
	int n;

	Binit(&b, 1, OWRITE);
	while(c = *fmt++){
		if(c == '\\'){
			c1 = *fmt++;
			switch(c1){
			default:
				Bputc(&b, c);
				Bputc(&b, c1);
				break;
			case '"':
			case '\\':
				Bputc(&b, c1);
				break;
			case 'a':
				Bputc(&b, '\a');
				break;
			case 'b':
				Bputc(&b, '\b');
				break;
			case 'c':
				goto end;
			case 'f':
				Bputc(&b, '\f');
				break;
			case 'n':
				Bputc(&b, '\n');
				break;
			case 'r':
				Bputc(&b, '\r');
				break;
			case 't':
				Bputc(&b, '\t');
				break;
			case 'v':
				Bputc(&b, '\v');
				break;
			case '0':
			case '1':
			case '2':
			case '3':
			case '4':
			case '5':
			case '6':
			case '7':
				fmt--;
				n = puto(&b, fmt);
				if(n > 0)
					fmt += n;
				break;
			case 'x':
				n = putx(&b, fmt);
				if(n > 0)
					fmt += n;
				break;
			}
		}else if(c == '%'){
			n = format(&b, fmt, &fmt, args);
			if(n > 0){
				narg -= n;
				args += n;
			}
		}else
			Bputc(&b, c);
	}
end:
	Bflush(&b);
}

int
puto(Biobuf *b, char *s)
{
	int c, x;
	char *p, *e;

	c = 0;
	e = s + 3;
	for(p = s; p < e && *p; p++){
		x = cton(*p);
		if(x < 0 || x >= 8)
			break;
		c <<= 3;
		c |= x;
	}
	Bputc(b, c);
	return p - s;
}

int
putx(Biobuf *b, char *s)
{
	int c, x;
	char *p, *e;

	c = 0;
	e = s + 2;
	for(p = s; p < e && *p; p++){
		x = cton(*p);
		if(x < 0 || x >= 16)
			break;
		c <<= 4;
		c |= x;
	}
	Bputc(b, c);
	return p - s;
}

int
cton(char c)
{
	if(c >= '0' && c <= '9')
		return c - '0';
	if(c >= 'A' && c <= 'Z')
		return c - 'A' + 10;
	if(c >= 'a' && c <= 'z')
		return c - 'a' + 10;
	return -1;
}

int
format(Biobuf *b, char *s, char **pp, char **args)
{
	String *fmt;
	char c, *p, buf[100], **ap;
	int n;
	vlong v;
	uvlong u;
	double d;

	ap = args;
	fmt = s_copy("%");
	if(fmt == nil)
		sysfatal("s_copy: %r");
	p = s;
	while(c = *p++)
		if(strchr("-+ #0123456789hl.", c))
			s_putc(fmt, c);
		else if(c == '*'){
			n = 0;
			if(*ap)
				n = atoi(*ap++);
			snprint(buf, sizeof buf, "%d", n);
			s_append(fmt, buf);
		}else if(c == 'd' || c == 'i'){
			s_putc(fmt, c);
			s_terminate(fmt);
			v = 0LL;
			if(*ap)
				v = strtoll(*ap++, nil, 0);
			Bprint(b, s_to_c(fmt), v);
			return result(fmt, p, pp, ap - args);
		}else if(strchr("ouxX", c)){
			s_putc(fmt, c);
			s_terminate(fmt);
			u = 0ULL;
			if(*ap)
				u = strtoull(*ap++, nil, 0);
			Bprint(b, s_to_c(fmt), u);
			return result(fmt, p, pp, ap - args);
		}else if(strchr("fFeEgG", c)){
			s_putc(fmt, c);
			s_terminate(fmt);
			d = 0.0;
			if(*ap)
				d = strtod(*ap++, nil);
			Bprint(b, s_to_c(fmt), d);
			return result(fmt, p, pp, ap - args);
		}else if(c == 'c'){
			s_putc(fmt, c);
			s_terminate(fmt);
			Bprint(b, s_to_c(fmt), *ap ? **ap : '\0');
			if(*ap)
				ap++;
			return result(fmt, p, pp, ap - args);
		}else if(c == 's'){
			s_putc(fmt, c);
			s_terminate(fmt);
			Bprint(b, s_to_c(fmt), *ap ? *ap : "");
			if(*ap)
				ap++;
			return result(fmt, p, pp, ap - args);
		}else
			break;

	fprint(2, "%s: %%%s: invalid directive\n", argv0, s);
	exits("format");
	return -1;
}

int
result(String *fmt, char *s, char **pp, int d)
{
	s_free(fmt);
	*pp = s;
	return d;
}
