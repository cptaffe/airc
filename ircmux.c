/*
 * irc multiplexor, redialer, logger
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <ctype.h>
#include "irc.h"

typedef struct Chan Chan;
struct Chan
{
	char *name;
	int ref;
};

Chan **chan;
int nchan;
Chan *findchan(char*);

typedef struct Client Client;
struct Client
{
	int fd;
	int id;
	Isub sub;
	List *rl;
	List *wl;
	char **chan;
	int nchan;
};

Client **client;
int nclient;

typedef struct Forker Forker;
struct Forker
{
	void (*fn)(void*);
	void *arg;
	int stack;
};

Channel *forkchan;
void forkthread(void*);

char *announceaddr;
char *logdir;
int	chatty;
int	chattyacme;
char	*fullname;	/* full name of user */
char *nicks[5];		/* nick name to use, with alternates */
int	nnicks;
char	*nick;
char	*server;
int	redial;
char *servername;
int debug;
int mainfd;
int usepass;
void logthread(void*);
void announcethread(void*);
void clientthread(void*);
char *esmprint(char*, ...);

char **join;
int njoin;

void
usage(void)
{
	fprint(2, "usage: ircmux [-r] [-a address] [-f fullname] [-l logdir] [-n nick] [-p passwd] server\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	int i;
	char *passwd;
	Channel *c;
	Isub sub;

	quotefmtinstall();

	passwd = nil;
	servername = nil;
	ARGBEGIN{
	case 'D':
		debug = 1;
		break;
	case 'V':
		chatty = 1;
		break;
	case 'f':
		fullname = EARGF(usage());
		break;
	case 'a':
		announceaddr = EARGF(usage());
		break;
	case 'j':
		findchan(EARGF(usage()))->ref++;
		break;
	case 'l':
		logdir = EARGF(usage());
		break;
	case 'n':
		if(nnicks < nelem(nicks))
			nicks[nnicks++] = EARGF(usage());
		else
			EARGF(usage());
		break;
	case 'r':
		redial = 1;
		break;
	case 'p':
		passwd = EARGF(usage());
		break;
	}ARGEND
	
	if(argc != 1)
		usage();
	server = argv[0];
	if(servername == nil)
		servername = server;

	ircinit();
	c = chancreate(sizeof(void*), 0);
	if(logdir){
		threadcreate(logthread, c, STACK);
		recvp(c);
	}
	if(announceaddr){
		threadcreate(announcethread, c, STACK);
		recvp(c);
	}
	forkchan = chancreate(sizeof(Forker), 0);
	threadcreate(forkthread, nil, STACK);
	chanfree(c);

	if(redial){
		memset(&sub, 0, sizeof sub);
		sub.die = 1;
		sub.ml = listalloc();
		sendp(subchan, &sub);
	}
	
	for(;;){
		fprint(2, "%T ircmux: dial %s\n", server);
		if(ircdial(server) < 0){
			if(redial){
				fprint(2, "%T ircmux: dial %s: %r\n", server);
				sleep(60*1000);
				continue;
			}
			sysfatal("dial %s: %r", server);
		}
		fprint(2, "%T dialed\n");
		if(login(fullname, nicks, nnicks, passwd) < 0){
			if(redial){
				fprint(2, "ircmux: dial %s: %r\n", server);
				sleep(60*1000);
				continue;
			}
			sysfatal("login failed");
		}
		for(i=0; i<nchan; i++)
			if(chan[i]->ref)
				sendp(writechan, esmprint("JOIN %s", chan[i]->name));
		if(redial){
			listget(sub.ml);
			sendp(writechan, nil);
			continue;
		}
		threadexits(nil);
	}
}

char*
evsmprint(char *s, va_list v)
{
	s = vsmprint(s, v);
	if(s == nil)
		sysfatal("out of memory");
	return s;
}

char*
esmprint(char *s, ...)
{
	char *t;
	va_list arg;
	
	va_start(arg, s);
	t = evsmprint(s, arg);
	va_end(arg);
	return t;
}

void*
emalloc(int n)
{
	void *v;
	
	v = mallocz(n, 1);
	if(v == nil)
		sysfatal("out of memory");
	return v;
}

char*
estrdup(char *s)
{
	char *t;
	
	t = strdup(s);
	if(t == nil)
		sysfatal("out of memory");
	return t;
}

void*
erealloc(void *v, int n)
{
	v = realloc(v, n);
	if(v == nil)
		sysfatal("out of memory");
	return v;
}

void
forkthread(void *v)
{
	Forker f;
	
	for(;;){
		recv(forkchan, &f);
		threadcreate(f.fn, f.arg, f.stack);
	}
}

void
mainthreadcreate(void (*fn)(void*), void *arg, int stack)
{
	Forker f;
	
	f.fn = fn;
	f.arg = arg;
	f.stack = stack;
	send(forkchan, &f);
}

List *loglist;

void
logthread(void *v)
{
	char *file, *chan, *msg, *p;
	int fd;
	Imsg *m;
	Isub sub;
	
	memset(&sub, 0, sizeof sub);
	sub.snoop = 1;
	sub.ml = listalloc();
	loglist = sub.ml;
	sendp(subchan, &sub);
	sendp(v, nil);
	
	while((m = listget(sub.ml)) != nil){
		msg = nil;
		chan = m->dst;
		if(nick==nil || irccistrcmp(chan, nick) == 0)
			chan = m->src;
		if(chan[0] == '*'){
			free(m);
			continue;
		}

		if(irccistrcmp(m->cmd, "JOIN") == 0)
			msg = esmprint("+%s", m->src);
		else if(irccistrcmp(m->cmd, "PART") == 0){
			if(m->arg[0][0])
				msg = esmprint("-%s [%s]", m->src, m->arg[0]);
			else
				msg = esmprint("-%s", m->src);
		}else if(irccistrcmp(m->cmd, "PRIVMSG") == 0)
			msg = esmprint("<%s> %s", m->src, m->arg[0]);
		else if(m->cmdnum == 352){
			chan = m->arg[0];
			msg = esmprint("[%s %s %s %s]", m->arg[1], m->arg[2], m->arg[3], m->arg[4]);
		}
		else if(m->cmdnum == 353){
			chan = m->arg[1];
			msg = esmprint("[%s %s]", m->arg[0], m->arg[2]);
		}
		
		if(msg == nil){
			free(m);
			continue;
		}
		
		file = esmprint("%s/%s", logdir, chan);
		ircstrlwr(file+strlen(logdir)+1);
		for(p=file+strlen(logdir)+1; *p; p++)
			if(*p == '/')
				*p = '_';
		
		if((fd = open(file, OWRITE)) >= 0
		|| (fd = create(file, OEXCL|OWRITE, 0666)) >= 0){
			seek(fd, 0, 2);
			fprint(fd, "%T %s\n", msg);
			close(fd);
		}
		free(msg);
		free(m);
	}
	sendp(unsubchan, &sub);
}

Chan*
findchan(char *name)
{
	int i;
	Chan *c;
	
	for(i=0; i<nchan; i++)
		if(irccistrcmp(chan[i]->name, name) == 0)
			return chan[i];
	c = emalloc(sizeof(Chan));
	c->name = estrdup(name);
	chan = erealloc(chan, (nchan+1)*sizeof chan[0]);
	chan[nchan++] = c;
	return c;
}

void
dropchan(Chan *ch)
{
	int i;
	
	for(i=0; i<nchan; i++){
		if(chan[i] == ch){
			chan[i] = chan[--nchan];
			break;
		}
	}
	free(ch);
}

Client*
newclient(int fd)
{
	Client *cl;
	static int idgen;
	
	cl = emalloc(sizeof(Client));
	cl->fd = fd;
	cl->id = ++idgen;
	client = erealloc(client, (nclient+1)*sizeof client[0]);
	client[nclient++] = cl;
	return cl;
}

void
dropclient(Client *cl)
{
	int i;
	
	for(i=0; i<nclient; i++){
		if(client[i] == cl){
			client[i] = client[--nclient];
			break;
		}
	}
}

void announceproc(void *v);

void
announcethread(void *v)
{
	int fd;
	char adir[100];
	
	if((fd = announce(announceaddr, adir)) < 0)
		sysfatal("announce %s: %r", announceaddr);
	proccreate(announceproc, estrdup(adir), STACK);
	sendp(v, nil);
}

void
announceproc(void *adir)
{
	int fd, lfd;
	char ldir[100];
	Client *cl;
	
	while((lfd = listen(adir, ldir)) >= 0){
		fd = accept(lfd, ldir);
		close(lfd);
		cl = newclient(fd);
		mainthreadcreate(clientthread, cl, STACK);
	}
}

void ircclearmsg(Imsg*);

Imsg*
ircparseclientmsg(char *p)
{
	char *f[MAXARG+3], *q, *ep;
	int nf, n;
	Imsg *m;

	n = sizeof(Imsg)+3*(strlen(p)+1);
	m = emalloc(n);
	ircclearmsg(m);
	memset(f, 0, sizeof f);
	m->len = n;
	q = p;
	p = (char*)&m[1];
	strcpy(p, q);
	m->raw = p;
	p += strlen(p)+1;
	strcpy(p, q);
	ep = p+strlen(p)+1;	/* room for more data */
	if((nf=irctokenize(p, f, nelem(f))) == nelem(f)){
		free(m);
		werrstr("too many args");
		return nil;
	}
	if(nf < 1){
		free(m);
		werrstr("too few args");
		return nil;
	}
	n = 0;
	m->cmd = f[n++];
	m->narg = nf-n;
	if(m->narg)
		memmove(m->arg, f+n, m->narg*sizeof(char*));
	return m;
}

void
clientreadproc(void *v)
{
	char *p;
	Biobuf *b;
	Client *cl;
	Imsg *m;
	
	cl = v;
	b = emalloc(sizeof(Biobuf));
	Binit(b, cl->fd, OREAD);
	while((p = Brdline(b, '\n')) != nil){
		p[Blinelen(b)-1] = 0;
		if(p[Blinelen(b)-2] == '\r')
			p[Blinelen(b)-2] = 0;
		if(chatty)
			fprint(2, "%T cl%d -> %s\n", cl->id, p);
		m = ircparseclientmsg(p);
		if(m == nil){
			fprint(2, "%T cl%d: bad message: %s\n", cl->id, p);
			continue;
		}
		listput(cl->rl, m);
	}
	listput(cl->rl, nil);
}

void
clientwriteproc(void *v)
{
	char *p;
	Client *cl;
	
	cl = v;
	while((p = listget(cl->wl)) != nil){
		if(chatty)
			fprint(2, "%T cl%d <- %s\n", cl->id, p);
		write(cl->fd, p, strlen(p));
		write(cl->fd, "\r\n", 2);
		free(p);
	}
	listfree(cl->wl);
	listfree(cl->rl);
	listfree(cl->sub.ml);
	free(cl);
}

int
joinchan(Client *cl, char *name, int up)
{
	int i;
	
	for(i=0; i<cl->nchan; i++){
		if(irccistrcmp(cl->chan[i], name) == 0){
			if(up)
				return 0;
			else{
				free(cl->chan[i]);
				cl->chan[i] = cl->chan[--cl->nchan];
				return 1;
			}
		}
	}
	if(!up)
		return 0;
	cl->chan = erealloc(cl->chan, (cl->nchan+1)*sizeof(cl->chan[0]));
	cl->chan[cl->nchan++] = estrdup(name);
	return 1;
}

void
clientthread(void *v)
{
	int i, handled;
	Alt a[3];
	Imsg *m, *mm;
	Chan *ch;
	Client *cl;
	char *you;
	
	cl = v;
	if(chatty)
		fprint(2, "%T new client cl%d\n", cl->id);
	memset(&cl->sub, 0, sizeof cl->sub);
	cl->sub.snoop = 1;
	cl->sub.ml = listalloc();
	sendp(subchan, &cl->sub);
	
	cl->rl = listalloc();
	cl->wl = listalloc();
	proccreate(clientwriteproc, cl, STACK);
	proccreate(clientreadproc, cl, STACK);
	
	a[0].op = CHANRCV;
	a[0].c = cl->sub.ml->rd;
	a[0].v = &m;
	
	a[1].op = CHANRCV;
	a[1].c = cl->rl->rd;
	a[1].v = &m;
	
	a[2].op = CHANEND;
	
	you = estrdup("gre");
	for(;;){
		switch(alt(a)){
		case 0:
			if(strcmp(m->cmd, "PING") == 0 
			|| strstr(m->arg[0], "\001VERSION\001")){
				free(m);
				break;
			}
			listput(cl->wl, estrdup(m->raw));
			free(m);
			break;

		case 1:
			if(m == nil)
				goto done;

			handled = 0;
			fprint(2, "%T msg: %M\n", m);
			if(irccistrcmp(m->cmd, "PONG") == 0){
				handled = 1;
			}
			if(irccistrcmp(m->cmd, "PASS") == 0){
				handled = 1;
			}
			if(irccistrcmp(m->cmd, "USER") == 0){
				if(m->arg[0] && m->arg[0][0]){
					free(you);
					you = estrdup(m->arg[0]);
				}
				listput(cl->wl, esmprint(":ircmux 001 %s :welcome to the ircmux proxy", you));
				for(i=0; i<nchan; i++){
					if(chan[i]->ref)
						listput(cl->wl, esmprint(":%s!ircmux JOIN %s", you, chan[i]->name));
				}
				handled = 1;
			}
			if(irccistrcmp(m->cmd, "NICK") == 0){
				handled = 1;
			}
			if(irccistrcmp(m->cmd, "PRIVMSG") == 0){
				for(i=0; i<nclient; i++)
					if(client[i] != cl)
						listput(client[i]->wl, esmprint(":%s!ircmux %s", you, m->raw));
				if(loglist){
					mm = copymsg(m);
					/* src='' dst='' cmd=PRIVMSG arg0='#test' arg1='message' */
					/* => src=you dst='#test' cmd=PRIVMSG arg1='message' */
					mm->src = you;
					mm->dst = mm->arg[0];
					mm->arg[0] = mm->arg[1];
					mm->arg[1] = mm->arg[2];
					fprint(2, "log %M\n", mm);
					listput(loglist, mm);
				}
				handled = 0;	/* send to server too */
			}
			if(irccistrcmp(m->cmd, "JOIN") == 0){
				handled = 1;
				if(m->arg[0] == nil){
					listput(cl->wl, esmprint(":ircmux 403 %s :no channel name", you));
				}else if(m->arg[0][0] != '#'){
					listput(cl->wl, esmprint(":ircmux 403 %s :no such channel %s", you, m->arg[0]));
				}else{
					if(joinchan(cl, m->arg[0], 1)){
						ch = findchan(m->arg[0]);
						if(ch->ref == 0)
							handled = 0;
						ch->ref++;
					}
					if(handled)
						listput(cl->wl, esmprint(":%s!ircmux JOIN %s", you, m->arg[0]));
				}
			}
			if(irccistrcmp(m->cmd, "PART") == 0){
				handled = 1;
				if(m->arg[0] == nil){
					listput(cl->wl, esmprint(":ircmux 403 %s :no channel name", you));
				}else{
					ch = findchan(m->arg[0]);
					if(ch->ref == 0){
						dropchan(ch);
						listput(cl->wl, esmprint(":ircmux 442 %s :not on channel", you));
					}else{
						if(joinchan(cl, m->arg[0], 0)){
							ch->ref--;
							if(ch->ref == 0){
								dropchan(ch);
								handled = 0;
							}
						}
						if(handled)
							listput(cl->wl, esmprint(":%s!ircmux PART %s", you, m->arg[0]));
					}
				}
			}
			if(irccistrcmp(m->cmd, "QUIT") == 0){
				listput(cl->wl, estrdup("ERROR :client sent quit"));
				/* wait for eof */
				handled = 1;
			}
			if(!handled)
				sendp(writechan, estrdup(m->raw));
			free(m);
			break;
		}
	}
done:
	sendp(unsubchan, &cl->sub);
	for(i=0; i<cl->nchan; i++){
		ch = findchan(cl->chan[i]);
		if(--ch->ref == 0)
			dropchan(ch);
	}
	dropclient(cl);
	listput(cl->wl, nil);
	free(you);
}

