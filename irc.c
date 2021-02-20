#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <9pclient.h>
#include "acme.h"
#include "irc.h"

int ircfd;
char *ircaddr;
char *nick;
Ichan **ichan;
int	nichan;

Channel	*readchan;	/* chan(Imsg*) -- parsed input messages */
Channel	*writechan;	/* chan(char*) -- unparsed output messages */
Channel	*subchan;		/* chan(Isub*) -- subscribe */
Channel	*unsubchan;	/* chan(Isub*) -- unsubscribe */

static char Empty[] = "";

int
timefmt(Fmt *fmt)
{
	char *p;
	
	p = ctime(time(0));
	p[19] = 0;
	return fmtstrcpy(fmt, p+4);
}

void
ircinit(void)
{
	fmtinstall('M', imsgfmt);
	fmtinstall('T', timefmt);
	quotefmtinstall();

	readchan = chancreate(sizeof(Imsg*), 16);
	writechan = chancreate(sizeof(char*), 16);
	subchan = chancreate(sizeof(Isub*), 0);
	unsubchan = chancreate(sizeof(Isub*), 0);
	threadcreate(muxthread, nil, STACK);
	threadcreate(pingthread, nil, STACK);
}

int
ircdial(char *addr)
{
	int fd;
	
	ircaddr = estrdup(netmkaddr(addr, "tcp", "6667"));
	if((fd = dial(ircaddr, nil, nil, nil)) < 0)
		return -1;
	proccreate(ircread, (void*)dup(fd, -1), STACK);
	proccreate(ircwrite, (void*)dup(fd, -1), STACK);
	return 0;
}

/* 
 * Tokenize an incoming message according to the IRC rules.
 * Spaces separate fields and : beginning a field other than the first
 * is a super quote.
 */
int
irctokenize(char *s, char **args, int maxargs)
{
	int nargs;
	char *os;

	os = s;
	for(nargs=0; nargs<maxargs; nargs++){
		while(*s==' ')
			*s++ = '\0';
		if(*s == '\0')
			break;
		args[nargs] = s;
		if(s[0] == ':' && s != os){	/* super quote */
			args[nargs++]++;
			break;
		}
		while(*s != ' ' && *s != '\0')
			s++;
	}

	return nargs;
}

/*
 * This is cistrcmp except that {}|^ are lower-case for []\~.
 */
static int
irctolower(int c)
{
	if('A' <= c && c <= 'Z')
		return c+'a'-'A';
	if(c == '{')
		return '[';
	if(c == '}')
		return ']';
	if(c == '|')
		return '\\';
	if(c == '^')
		return '~';
	return c;
}

char*
ircstrlwr(char *s)
{
	char *t;
	
	for(t=s; *t; t++)
		*t = irctolower(*t);
	return s;
}

int
irccistrcmp(char *s, char *t)
{
	int a, b;

	if(s == t)
		return 0;
	if(s == nil)
		return -1;
	if(t == nil)
		return 1;
	for(; *s || *t; s++, t++){
		a = irctolower(*s);
		b = irctolower(*t);
		if(a < b)
			return -1;
		if(a > b)
			return 1;
	}
	return 0;
}

/*
 * Print a message.
 */
int
imsgfmt(Fmt *fmt)
{
	int i;
	Imsg *m;

	m = va_arg(fmt->args, Imsg*);
	fmtprint(fmt, "pre=%q src=%q dst=%q cmd=%q", 
		m->prefix, m->src, m->dst, m->cmd);
	for(i=0; i<m->narg; i++)
		fmtprint(fmt, " %q", m->arg[i]);
	return 0;
}

/*
 * Copy an imsg structure, preserving the fact that
 * free(i) is enough to free the whole message.
 */
Imsg*
copymsg(Imsg *i)
{
	int j, diff;
	Imsg *n;

	n = emalloc(i->len);
	memmove(n, i, i->len);
	diff = (char*)n - (char*)i;
	n->raw += diff;
	if(n->prefix != Empty)
		n->prefix += diff;
	if(n->src != Empty)
		n->src += diff;
	if(n->dst != Empty)
		n->dst += diff;
	if(n->cmd != Empty)
		n->cmd += diff;
	for(j=0; j<n->narg; j++){
		if(n->arg[j] != Empty)
			n->arg[j] += diff;
	}
	return n;
}

/*
 * Clear strings to zeros to avoid nil derefs.
 * Assumes structure has already been zeroed to handle other fields.
 */
void
ircclearmsg(Imsg *m)
{
	int i;
	
	m->prefix = Empty;
	m->src = Empty;
	m->dst = Empty;
	m->cmd = Empty;
	
	for(i=0; i<MAXARG; i++)
		m->arg[i] = Empty;
}

/*
 * Parse an IRC message.
 */
Imsg*
ircparsemsg(char *p)
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
	if(f[0][0] == ':'){
		m->prefix = f[0]+1;
		m->src = ep;
		strcpy(ep, m->prefix);
		if((q = strchr(ep, '!')) != nil){
			*q++ = 0;
			ep = q;
		}else if((q = strchr(ep, '@')) != nil){
			*q++ = 0;
			ep = q;
		}else
			ep = ep+strlen(ep)+1;
		USED(ep);
		n++;
	}
	m->cmd = f[n++];
	if(isdigit(m->cmd[0])
	&& isdigit(m->cmd[1])
	&& isdigit(m->cmd[2])
	&& m->cmd[3]==0)
		m->cmdnum = strtol(m->cmd, 0, 10);

	if(m->prefix != Empty){
		/*
		 * Messages with prefixes seem to put a destination
		 * (either our name or a channel) here.  I can't find
		 * any justification for this in the RFC though.  Oh well.
		 */
		m->dst = f[n++];
	}
	if(n > nf){
		free(m);
		werrstr("too few args");
		return nil;
	}
	m->narg = nf-n;
	if(m->narg)
		memmove(m->arg, f+n, m->narg*sizeof(char*));
	return m;
}

/* 
 * Read messages from the IRC fd and send on readchan.
 */
void
ircread(void *v)
{
	char *p;
	Biobuf *b;
	Imsg *m;
	int fd;

	fd = (int)v;
	threadsetname("ircread");
	b = emalloc(sizeof(Biobuf));
	Binit(b, fd, OREAD);
	while((p = Brdline(b, '\n')) != nil){
		p[Blinelen(b)-1] = '\0';
		if(p[Blinelen(b)-2] == '\r')
			p[Blinelen(b)-2] = '\0';
		if(chatty)
			fprint(2, "%T << %s\n", p);
		if((m = ircparsemsg(p)) == nil){
			fprint(2, "irc: bad message (%r):\n\t%s\n", p);
			continue;
		}
		sendp(readchan, m);
	}
	sendp(readchan, nil);
}

/*
 * Receive strings from writechan, write them to IRC fd, and free them.
 */
void
ircwrite(void *v)
{
	char *p;
	int fd;
	
	fd = (int)v;
	threadsetname("ircwrite");
	while((p = recvp(writechan)) != nil){
		if(chatty)
			fprint(2, "%T >> %s\n", p);
		fprint(fd, "%s\r\n", p);
		free(p);
	}
}

/*
 * For debugging: relay lines from standard input
 * straight to the IRC fd.
 */
void
inputrelay(void *v)
{
	char *p;
	Biobuf *b;
	Ichan *ic;
	char buf[1024];
	
	ic = v;
	threadsetname("inputrelay");
	b = emalloc(sizeof(Biobuf));
	Binit(b, 0, OREAD);
	while((p = Brdstr(b, '\n', 1)) != nil){
		if(ic){
			snprint(buf, sizeof buf, "PRIVMSG %s :%s", ic->name, p);
			sendp(writechan, estrdup(buf));
			free(p);
		}else
			sendp(writechan, p);
	}
	free(b);
//	close(netfd);
}

/*
 * Log in using NICK and USER, and then 
 * msg nickserv if necessary.
 */
int
login(char *fullname, char **nicks, int nnicks, char *passwd)
{
	char buf[512];
	Isub sub;
	Imsg *m;
	int i;

	threadsetname("loginthread");
	memset(&sub, 0, sizeof sub);
	sub.snoop = 1;
	sub.ml = listalloc();
	sendp(subchan, &sub);

	if(nnicks == 0)
		nicks[nnicks++] = getuser();
	if(fullname == nil)
		fullname = "Acme User";

	if(passwd && usepass){
		snprint(buf, sizeof buf, "PASS :%s", passwd);
		sendp(writechan, estrdup(buf));
	}
	snprint(buf, sizeof buf, "USER %s 0 * :%s", nicks[0], fullname);
	sendp(writechan, estrdup(buf));
	if(usepass){	/* assume talking to proxy */
		sendp(writechan, estrdup("PATTACH Freenode"));
		sendp(unsubchan, &sub);
		nick = nicks[0];
		return 0;
	}
	for(i=0; i<nnicks; i++){
		nick = nicks[i];
		snprint(buf, sizeof buf, "NICK %s", nick);
		sendp(writechan, estrdup(buf));

		/* wait for RPL_WELCOME message */
		/* if get error message, try next */
		for(;;){
			m = listget(sub.ml);
			if(strcmp(m->cmd, "001") == 0){
				free(m);
				goto Nickaccepted;
			}
			if(m->cmdnum == 451){
				/* from the failed USER command */
				free(m);
				continue;
			}
			if(m->cmd[0] == '4'){
				free(m);
				break;
			}
			free(m);
		}
	}
	sendp(unsubchan, &sub);
	return -1;

Nickaccepted:

	if(passwd && !usepass){
		/* send password and wait for mode change */
		snprint(buf, sizeof buf, "PRIVMSG nickserv :IDENTIFY %s", passwd);
		sendp(writechan, estrdup(buf));
		for(;;){
			m = listget(sub.ml);
			if(irccistrcmp(m->src, "nickserv") == 0)
				break;
			if(irccistrcmp(m->cmd, "MODE") == 0){
				if(m->narg >= 2
				&& irccistrcmp(m->arg[0], nick) == 0
				&& m->arg[1][0] == '+'
				&& strpbrk(m->arg[1], "er") != nil)
					break;
				if(m->narg >= 1
				&& m->arg[0][0] == '+'
				&& strpbrk(m->arg[0], "er") != nil)
					break;
			}
			free(m);
		}
		free(m);
	}

	sendp(unsubchan, &sub);
	return 0;
}

/*
 * Print all otherwise unwanted traffic.  Mostly for debugging.
 */
void
printmopthread(void *v)
{
	char *s;
	Imsg *m;
	Isub sub;

	threadsetname("mopthread");
	s = v;
	memset(&sub, 0, sizeof sub);
	if(strcmp(s, "mop") == 0)
		sub.mop = 1;
	else if(strcmp(s, "snoop") == 0)
		sub.snoop = 1;
	else
		abort();

	sub.ml = listalloc();
	sendp(subchan, &sub);

	for(;;){
		m = listget(sub.ml);
		print("mop: %M\n", m);
		free(m);
	}
}

/*
 * Respond to pings.
 */
int
pingmatch(Isub *i, Imsg *m)
{
	USED(i);
	return strcmp(m->cmd, "PING")==0;
}

void
pingthread(void *v)
{
	Imsg *m;
	Isub sub;
	char buf[512];
	char *src;

	USED(v);
	threadsetname("pingthread");
	memset(&sub, 0, sizeof sub);
	sub.match = pingmatch;
	sub.ml = listalloc();
	sendp(subchan, &sub);

	for(;;){
		m = listget(sub.ml);
		if(m->narg)
			src = m->arg[0];
		else
			src = "you";
		snprint(buf, sizeof buf, "PONG :%s", src);
		sendp(writechan, estrdup(buf));
		free(m);
	}
}

/*
 * Multiplex the input among the many clients.
 */
void
muxthread(void *v)
{
	Alt a[4];
	Isub *s, **sub;
	int nsub, n, nn;
	Imsg *m;
	int i;
	enum {
		Csub,
		Cunsub,
		Cread,
		Cend,
	};

	nsub = 0;
	sub = nil;

	threadsetname("muxthread");
	a[Csub].op = CHANRCV;
	a[Csub].c = subchan;
	a[Csub].v = &s;

	a[Cunsub].op = CHANRCV;
	a[Cunsub].c = unsubchan;
	a[Cunsub].v = &s;

	a[Cread].op = CHANRCV;
	a[Cread].c = readchan;
	a[Cread].v = &m;

	a[Cend].op = CHANEND;

	for(;;){
		switch(alt(a)){
		case Csub:	/* subscribe */
			dprint("sub\n");
			sub = erealloc(sub, (nsub+1)*sizeof(sub[0]));
			sub[nsub++] = s;
			break;

		case Cunsub:	/* unsubscribe */
			dprint("unsub\n");
			for(i=0; i<nsub; i++)
				if(sub[i] == s){
					sub[i] = sub[--nsub];
					goto found;
				}
			fprint(2, "unexpected unsub %p (%d)\n", s, nsub);
		found:
			break;

		case Cread:
			dprint("read nsub=%d %p/%M\n", nsub, m, m);
			/* dispatch new message to those who want it */
			if(m == nil){
				fprint(2, "lost network connection\n");
				n = 0;
				for(i=0; i<nsub; i++){
					if(sub[i]->die){
						listput(sub[i]->ml, nil);
						n++;
					}
				}
				if(n == 0)
					threadexitsall(nil);
				continue;
			}
			n = 0;
			for(i=0; i<nsub; i++){
				nn = 0;
				if(sub[i]->snoop
				|| (!sub[i]->mop && sub[i]->match && (nn=sub[i]->match(sub[i], m)))){
					listput(sub[i]->ml, copymsg(m));
					if(!sub[i]->snoop)
						n += nn;
				}
			}
			if(n == 0){
				for(i=0; i<nsub; i++)
					if(sub[i]->mop)
					if(sub[i]->match && sub[i]->match(sub[i], m)){
						listput(sub[i]->ml, copymsg(m));
						n++;
					}
			}
			if(n == 0){
				for(i=0; i<nsub; i++)
					if(sub[i]->mop && !sub[i]->match)
						listput(sub[i]->ml, copymsg(m));
			}
			free(m);
			break;
		}
	}
}

/*
 * Maintain metadata about a particular channel.
 *
 * Most metadata messages put the channel name as the
 * first argument.  The names list puts it as the second.
 */
int
ichanmatch(Isub *s, Imsg *m)
{
	Ichan *ic;
	char *p;
	
	ic = s->aux;
	
	if(irccistrcmp(m->dst, ic->name)==0)
		return 1;

	if(irccistrcmp(m->cmd, "PRIVMSG") == 0)
	if(irccistrcmp(m->dst, nick) == 0){
		p = strchr(m->prefix, '!');
		if(p)
			*p = 0;
		if(irccistrcmp(m->prefix, ic->name) == 0){
			if(p)
				*p = '!';
			return 1;
		}
	}

	if(irccistrcmp(m->cmd, "NICK")==0 
		|| irccistrcmp(m->cmd, "QUIT")==0)
	if(inchannel(m->src, ic))
		return 1;

	if(m->narg>0 && irccistrcmp(m->arg[0], ic->name)==0)
		return 1;

	if(m->narg>1 && m->cmdnum==RPL_NAMREPLY
	&& irccistrcmp(m->arg[1], ic->name) == 0)
		return 1;

	return 0;
}

static int
whocmp(const void *va, const void *vb)
{
	Iwho *a = (Iwho*)va;
	Iwho *b = (Iwho*)vb;
	return irccistrcmp(a->nick, b->nick);
}

void
sortwho(Ichan *ic)
{
	qsort(ic->who, ic->nwho, sizeof(ic->who[0]), whocmp);
}

void
freewho(Ichan *ic)
{
	int i;
	
	for(i=0; i<ic->nwho; i++){
		free(ic->who[i].user);
		free(ic->who[i].host);
		free(ic->who[i].server);
		free(ic->who[i].nick);
		free(ic->who[i].fullname);
	}
	ic->nwho = 0;
}

void
addwho(Ichan *ic, Imsg *m)
{
	Iwho wx, *w;
	char **arg, *p;
	int n, narg;

fprint(2, "addwho: narg=%d arg1=%s\n", m->narg, m->arg[0]);
	arg = m->arg;
	narg = m->narg;
	if(narg < 5)
		return;
	arg++; narg--;	/* skip channel */
	wx.hops = 0;
	wx.user = *arg++; narg--;
	wx.host = *arg++; narg--;
	wx.server = *arg++; narg--;
	wx.nick = *arg++; narg--;
	wx.fullname = "";
	wx.mode[0] = 0;
	n = 0;
	if(**arg == 'H' || **arg == 'G')
	if((*arg)[1] == 0){
		wx.mode[n++] = **arg;
		arg++, narg--;
		if(narg == 0)
			goto out;
	}
	if(**arg == '*' && **arg == 0){
		wx.mode[n++] = '*';
		arg++, narg--;
		if(narg == 0)
			goto out;
	}
	if(**arg == '@' || **arg == '+')
	if((*arg)[1] == 0){
		wx.mode[n++] = **arg;
		arg++, narg--;
		if(narg == 0)
			goto out;
	}
	if(narg < 1)
		goto out;
	wx.hops = strtol(*arg, &p, 0);
	while(*p && isspace(*p))
		p++;
	wx.fullname = p;

out:
	ic->who = erealloc(ic->who, (ic->nwho+1)*sizeof(ic->who[0]));
	w = &ic->who[ic->nwho++];
	*w = wx;
	w->user = estrdup(w->user);
	w->host = estrdup(w->host);
	w->server = estrdup(w->server);
	w->nick = estrdup(w->nick);
	w->fullname = estrdup(w->fullname);
}

static int
findnick(Ichan *ic, char *name)
{
	int i;
	
	for(i=0; i<ic->nwho; i++)
		if(irccistrcmp(ic->who[i].nick, name) == 0)
			return i;
	return -1;
}

void
addname(Ichan *ic, char *name)
{
	int i;
	Iwho *w;
	
	i = findnick(ic, name);
	if(i >= 0)
		return;
	ic->who = erealloc(ic->who, (ic->nwho+1)*sizeof(ic->who[0]));
	w = &ic->who[ic->nwho++];
	memset(w, 0, sizeof *w);
	w->user = estrdup("");
	w->host = estrdup("");
	w->server = estrdup("");
	w->nick = estrdup(name);
	w->fullname = estrdup("");
}

void
delname(Ichan *ic, char *name)
{
	int i;
	
	i = findnick(ic, name);
	if(i < 0)
		return;
	ic->nwho--;
	memmove(ic->who+i, ic->who+i+1, (ic->nwho-i)*sizeof(ic->who[0]));
}

void
changename(Ichan *ic, char *old, char *name)
{
	int i;
	
	i = findnick(ic, old);
	if(i < 0)
		addname(ic, name);
	else{
		free(ic->who[i].nick);
		ic->who[i].nick = estrdup(name);
	}
}
		
void
ichanthread(void *v)
{
	char buf[512], *p, *nextp;
	Ichan *ic;
	Imsg *m;
	int joined, handled, gotnames;

	ic = v;
	threadsetname(ic->name);
	sendp(subchan, &ic->sub);
	gotnames = 0;
	joined = 0;

	if(ic->name[0] == '#'){
		if(!ic->sure){
			/* channel, need to join */
			snprint(buf, sizeof buf, "JOIN :%s", ic->name);
			sendp(writechan, estrdup(buf));
			/* will process the rest of the messages as they arrive */
		}else{
			joined = 1;
			qunlock(&ic->q);
		}
	}else{
		/* user, sanity check with whois */
		if(!ic->sure){
			snprint(buf, sizeof buf, "WHOIS :%s", ic->name);
			sendp(writechan, estrdup(buf));
		}else{
			joined = 1;
			qunlock(&ic->q);
		}
	}

	while((m = listget(ic->sub.ml)) != nil){
		handled = 0;
		if(!joined){
			if(m->cmdnum != 477)
			if(400 <= m->cmdnum && m->cmdnum <= 499){
				/* error, probably in join/whois */
				snprint(ic->err, ERRMAX, "%s", m->narg ? m->arg[m->narg-1] : m->cmd);
				qunlock(&ic->q);
				goto out;
			}
			if(ic->name[0] != '#' && m->cmdnum == RPL_ENDOFWHOIS){
				/* end of whois list, must have succeeded */
				qunlock(&ic->q);
				handled = 1;
				joined = 1;
			}
		}
		if(ic->name[0] == '#'){
			if(irccistrcmp(m->cmd, "JOIN") == 0){
				if(irccistrcmp(m->src, nick) == 0){	/* it's me! */
					if(!joined){
						joined = 1;
						qunlock(&ic->q);
					}
				}else{
					addname(ic, m->src);
				}
				handled = 0;	/* send to chatter */
			}
			if(irccistrcmp(m->cmd, "PART") == 0){
				delname(ic, m->src);
				handled = 0;	/* send to chatter */
			}
			if(irccistrcmp(m->cmd, "QUIT") == 0){
				delname(ic, m->src);
				handled = 0;	/* send to chatter */
			}
			if(irccistrcmp(m->cmd, "NICK") == 0){
				changename(ic, m->src, m->dst);
				handled = 0;	/* send to chatter */
			}
			if(m->cmdnum == RPL_NOTOPIC){
				free(ic->topic);
				ic->topic = nil;
				handled = 1;
			}
			if(m->cmdnum == RPL_TOPIC && m->narg == 1){
				free(ic->topic);
				ic->topic = estrdup(m->arg[0]);
				handled = 1;
			}
			if(m->cmdnum == RPL_OWNERTIME && m->narg == 2){
				free(ic->owner);
				ic->owner = estrdup(m->arg[0]);
				ic->time = strtol(m->arg[1], 0, 10);
				handled = 1;
			}
			if(m->cmdnum == RPL_NAMREPLY){	/* names */
				if(gotnames){
					/* ignore this; we already have a list */
				}else if(m->narg != 3
				|| strlen(m->arg[0]) != 1
				|| strchr("=*@", m->arg[0][0]) == nil
				|| irccistrcmp(m->arg[1], ic->name) != 0)
					print("bad names line: %M\n", m);
				else{
					for(p=m->arg[2]; *p; p=nextp){
						if((nextp = strchr(p, ' ')) == nil)
							nextp = p+strlen(p);
						else
							*nextp++ = 0;
						addname(ic, p);
					}
				}
				handled = 1;
			}
			if(m->cmdnum == RPL_ENDOFNAMES){	/* end of name list */
				gotnames = 1;
				handled = 1;
			}
			if(m->cmdnum == RPL_WHOREPLY){
				if(!ic->_inwho){
					freewho(ic);
					fprint(2, "freewho\n");
					ic->_inwho = 1;
				}
				fprint(2, "who %s %d\n", m->arg[2], ic->nwho);
				addwho(ic, m);
				handled = 1;
			}
			if(m->cmdnum == RPL_ENDOFWHO){
				ic->_inwho = 0;
				sortwho(ic);
				handled = 0;
			}	
		}
		if(!handled)
			listput(ic->chatter, m);
		else
			free(m);
	}
out:
	dprint("leaving %s\n", ic->name);
	/* we're leaving ... */
	if(m != nil){
		/* ... because there was an error on join */
		free(m);
	}else{
		if(ic->name[0] == '#')
			sendp(writechan, esmprint("PART :%s", ic->name));
	}

	sendp(unsubchan, &ic->sub);
	
	free(ic->name);
	listfree(ic->sub.ml);
	listfree(ic->chatter);
print("ichan %p\n", ic);
	free(ic);

	threadexits(nil);
}

Ichan*
ircjoin(char *name, int sure, char e[ERRMAX])
{
	int i;
	Ichan *ic;

	if(name[0]=='*' || strlen(name) > 128){
		strcpy(e, "bad channel/nick name");
		return nil;
	}

	for(i=0; i<nichan; i++){
		if(irccistrcmp(ichan[i]->name, name) == 0){
			ichan[i]->ref++;
			return ichan[i];
		}
	}

	/* build new ichan */
	ic = emalloc(sizeof(Ichan));
	ic->sure = sure;
	ic->name = estrdup(name);
	ic->sub.aux = ic;
	ic->sub.match = ichanmatch;
	ic->sub.ml = listalloc();
	ic->chatter = listalloc();
	ic->ref = 1;
	e[0] = 0;
	ichan = erealloc(ichan, (nichan+1)*sizeof(Ichan*));
print("add %d %p\n", nichan, ic);
	ichan[nichan++] = ic;

	/* prevent others from racing in while we dial */
	qlock(&ic->q);

	threadcreate(ichanthread, ic, STACK);

	/* ichanthread will unlock it when ready */
	qlock(&ic->q);

	if(ic->err[0]){
		strecpy(e, e+sizeof e, ic->err);
		qunlock(&ic->q);	/* BUG: can't free just yet */
		return nil;
	}
	return ic;	
}

void
ircleave(Ichan *ic)
{
	int i;
	
	if(--ic->ref > 0)
		return;

	for(i=0; i<nichan; i++){
		if(ichan[i] == ic){
print("del %d %p\n", i, ichan[i]);
			ichan[i] = ichan[--nichan];
			break;
		}
	}
	listput(ic->sub.ml, nil);
}

int
inchannel(char *who, Ichan *ic)
{
	int i;

	if(irccistrcmp(who, ic->name) == 0)
		return 1;
	for(i=0; i<ic->nwho; i++)
		if(irccistrcmp(who, ic->who[i].nick) == 0)
			return 1;
	return 0;
}

