#include	"u.h"
#include	"../port/lib.h"
#include	"mem.h"
#include	"dat.h"
#include	"fns.h"
#include	"../port/error.h"

#define IHASHSIZE	64
#define ihash(s)	imagealloc.hash[s%IHASHSIZE]

static	void	putsection(Section*);

enum
{
	NIMAGE = 200,
};

static struct Imagealloc
{
	Lock;
	Image	*free;
	Image	*hash[IHASHSIZE];
	Image	lru;
} imagealloc;

static	void	freeimage(Image*);
static	void	cleanimage(Image*);

void
initimage(void)
{
	Image *i, *ie;

	imagealloc.free = malloc(NIMAGE*sizeof(Image));
	if(imagealloc.free == nil)
		panic("imagealloc: no memory");
	ie = &imagealloc.free[NIMAGE-1];
	for(i = imagealloc.free; i < ie; i++)
		i->next = i+1;
	i->next = 0;
	imagealloc.lru.next = imagealloc.lru.prev = &imagealloc.lru;
	imagealloc.lru.ref = 1;
}

static Image*
imagereclaim(void)
{
	Image *i;

	i = imagealloc.lru.prev;
	if(i->next == i)
		return nil;
	lock(i);
	i->prev->next = i->next;
	i->next->prev = i->prev;
	unlock(&imagealloc);
	cleanimage(i);
	return i;
}

Image*
attachimage(Chan *c)
{
	Image *i, **l;

	lock(&imagealloc);

	/*
	 * Search the image cache for remains of the text from a previous
	 * or currently running incarnation
	 */
	for(i = ihash(c->qid.path); i; i = i->hash) {
		if(c->qid.path == i->qid.path) {
			lock(i);
			if(eqqid(c->qid, i->qid) &&
			   eqqid(c->mqid, i->mqid) &&
			   c->mchan == i->mchan &&
			   c->dev->dc == i->dc) {
//subtype
				incref(i);
				if(0 && i->ref == 1){	/* remove from LRU list */
					DBG("image %#p was LRU %s\n", i, c->path? c->path->s: "??");
					i->prev->next = i->next;
					i->next->prev = i->prev;
				}
				unlock(&imagealloc);
				return i;
			}
			unlock(i);
		}
	}

	/*
	 * imagereclaim frees the least-recently-used cached image
	 */
	if((i = imagealloc.free) == nil){
		i = imagereclaim();
		if(i == nil){
			i = mallocz(sizeof(*i), 1);
			if(i == nil)
				error(Enomem);
		}
	}else
		imagealloc.free = i->next;

	lock(i);
	i->ref = 1;
	incref(c);
	i->c = c;
	i->dc = c->dev->dc;
//subtype
	i->qid = c->qid;
	i->mqid = c->mqid;
	i->mchan = c->mchan;
	l = &ihash(c->qid.path);
	i->hash = *l;
	*l = i;
	unlock(&imagealloc);

	return i;
}

/*
 * i is locked
 */
static void
cleanimage(Image *i)
{
	Chan *c;
	Image *f, **l;
	int s;

	DBG("freeimage: %p %s\n", i, i->c && i->c->path? i->c->path->s: "?");

	l = &ihash(i->qid.path);
	mkqid(&i->qid, ~0, ~0, QTDIR);	/* now impossible to find by hash */
	unlock(i);

	c = i->c;
	i->c = nil;
	if(c == nil || c->ref == 0)
		panic("putimage: %#p %#p", c, getcallerpc(&i));

	lock(&imagealloc);
	for(f = *l; f; f = f->hash) {
		if(f == i) {
			*l = i->hash;
			break;
		}
		l = &f->hash;
	}
	unlock(&imagealloc);

	for(s = 0; s < nelem(i->section); s++){
		if(i->section[s] != nil){
			putsection(i->section[s]);
			i->section[s] = nil;
		}
	}

	/* let the daemon deal with it */
	ccloseq(c);
}

static void
freeimage(Image *i)
{
	cleanimage(i);

	lock(&imagealloc);
	i->next = imagealloc.free;
	imagealloc.free = i;
	unlock(&imagealloc);
}

void
putimage(Image *i)
{
	DBG("putimage: %p ref=%d\n", i, i->ref);
	lock(i);
	if(decref(i) != 0){
		unlock(i);
		return;
	}

	/* TO DO: LRU recycling, with a quick cull if memory runs low */

	freeimage(i);

}

Section*
newsection(uintptr size, ulong fstart, ulong flen)
{
	Section *s;
	int npages, lg2pgsize;

	lg2pgsize = PGSHFT;	/* TO DO: pick a page size */

	if(size & ((1<<lg2pgsize)-1))
		panic("newpages");

	npages = size>>lg2pgsize;
	if(npages > (SEGMAPSIZE*PTEPERTAB))
		error(Enovmem);

	s = smalloc(sizeof(*s) + npages*sizeof(Page*));
	s->fstart = fstart;
	s->flen = flen;
	s->xsize = size;
	s->npages = npages;
	s->lg2pgsize = lg2pgsize;
	return s;
}

static void
putsection(Section *s)
{
	int i;
	Page *p;

	for(i = 0; i < s->npages; i++){
		p = s->pages[i];
		if(p != nil){
			putpage(p);
			s->pages[i] = nil;
		}
	}
}

static void
faulterror(char *s, Chan *c, int freemem)
{
	char buf[ERRMAX];

	if(c && c->path){
		snprint(buf, sizeof buf, "%s accessing %s: %s", s, c->path->s, up->errstr);
		s = buf;
	}
	if(up->nerrlab) {
		postnote(up, 1, s, NDebug);
		error(s);
	}
	pexit(s, freemem);
}

/*
 * return a page from a text/data image, allocating and loading on demand if needed.
 */
Page*
imagepage(Image *image, int isec, uintptr addr, uintptr soff)
{
	Page *new, *ep, **pg;
	KMap *k;
	Chan *c;
	int n, ask;
	char *kaddr;
	ulong daddr;
	uintptr pgsize;
	Section *s;

	s = image->section[isec];
	pgsize = 1<<s->lg2pgsize;
	daddr = s->fstart+soff;

	DBG("read section %#p addr %#p o %#p da %lud sz %#p xsize %#p\n", s, addr, soff, daddr, pgsize, s->xsize);
	if(soff >= s->xsize)
		panic("imageread");
	pg = &s->pages[soff >> s->lg2pgsize];
	new = *pg;
	if(new != nil){
		incref(new);
		return new;
	}

	c = image->c;
	ask = s->flen-soff;
	if(ask > pgsize)
		ask = pgsize;

	new = newpage(0, s->lg2pgsize, nil);
	if(new == nil)
		panic("pio");	/* can't happen, ps wasn't locked */

	qlock(&s->lk);

	/* re-check under lock before starting IO */
	ep = *pg;
	if(ep != nil){
		qunlock(&s->lk);
		putpage(new);
		DBG("race %#p %#p -> %#p\n", s, soff, ep);
		incref(ep);
		return ep;
	}

	k = kmap(new);
	kaddr = VA(k);

	while(waserror()){
		if(strcmp(up->errstr, Eintr) == 0)
			continue;
		qunlock(&s->lk);
		kunmap(k);
		putpage(new);
		faulterror(Eioload, c, 0);
	}

	n = c->dev->read(c, kaddr, ask, daddr);
	if(n != ask)
		faulterror(Eioload, c, 0);
	if(ask < pgsize)
		memset(kaddr+ask, 0, pgsize-ask);

	poperror();
	kunmap(k);

	*pg = new;	/* update the page map */

	qunlock(&s->lk);

	incref(new);
	return new;
}
