#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include "irc.h"

void *emalloc(int);

/*
 * Arbitrary buffering for channels.
 */
typedef struct Elt Elt;
struct Elt
{
	Elt *next;
	void *v;
};

static void
bufferthread(void *v)
{
	Alt a[3];
	List *l;
	Elt *e, *ee;
	void *p;

	l = v;

	memset(a, 0, sizeof a);

	a[0].c = l->wr;	/* others write, we read */
	a[0].v = &p;
	a[0].op = CHANRCV;

	a[1].c = l->rd;	/* others read, we write */
	a[1].v = &p;
	a[1].op = CHANNOP;

	a[2].c = l->die;
	a[2].v = 0;
	a[2].op = CHANRCV;

	a[3].op = CHANEND;

	for(;;){
		if(l->head){
			a[1].op = CHANSND;
			p = ((Elt*)l->head)->v;
			// print("try rx %p\n", p);
		}else
			a[1].op = CHANNOP;

		switch(alt(a)){
		case 0:	/* receive */
			// print("rx\n");
			e = emalloc(sizeof(Elt));
			e->v = p;
			e->next = nil;
			if(l->head == nil)
				l->head = e;
			else
				((Elt*)l->tail)->next = e;
			l->tail = e;
			break;

		case 1:
			// print("tx\n");
			/* the value in l->head has been sent */
			e = l->head;
			l->head = e->next;
			free(e);
			break;

		case 2:
			// print("die\n");
			goto out;
		}
	}
out:
	for(e=l->head; e; e=ee){
		ee = e->next;
		free(e->v);
		free(e);
	}
	chanfree(l->wr);
	chanfree(l->rd);
	chanfree(l->die);
	free(l);
}

List*
listalloc(void)
{
	List *l;

	l = emalloc(sizeof(List));
	l->rd = chancreate(sizeof(void*), 0);
	l->wr = chancreate(sizeof(void*), 0);
	l->die = chancreate(sizeof(void*), 0);
	threadcreate(bufferthread, l, STACK);
	return l;
}

void
listfree(List *l)
{
	sendp(l->die, 0);
}

void*
listget(List *l)
{
	void *v;
	v = recvp(l->rd);
//	fprint(2, "listget %p\n", v);
	return v;
}

void
listput(List *l, void *v)
{
//	fprint(2, "listput %p\n", v);
	sendp(l->wr, v);
}
