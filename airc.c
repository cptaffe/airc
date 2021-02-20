/*
 * Irc client for acme - really generic chat I hope.
 *
 * To do: eliminate race in chatwin.
 */

#include <u.h>
#include <libc.h>
#include <bio.h>
#include <thread.h>
#include <ctype.h>
#include <9pclient.h>
#include "acme.h"
#include "irc.h"

int	chatty;
int	chattyacme;
char	*fullname;	/* full name of user */
char *nicks[5];		/* nick name to use, with alternates */
int	nnicks;
char	*nick;
char	*addr;
char	*server;
char *servername;
int debug;
Win	*ircwin;
int mainfd;
int usepass;

void autowinthread(void*);
void infothread(void*);
void	mainwin(Win*);
void	chatwin(void*);
void listthread(void*);
uint	doline(Win *w, char *name, char *line, uint q0, uint q1);
void newchat(char*, Imsg*);

void
usage(void)
{
	fprint(2, "usage: airc [-r] [-f fullname] [-n nick] server\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char **argv)
{
	char *irccmd;
	int efd, i, l;
	Win *w;
	char *passwd;
	Channel *c;

	quotefmtinstall();
	fmtinstall('E', eventfmt);

	l = 1;
	for(i=0; i<argc; i++)
		l += 2+2*strlen(argv[i])+1;
	irccmd = emalloc(l);
	irccmd[0] = 0;
	for(i=0; i<argc; i++){
		if(i)
			strcat(irccmd, " ");
		snprint(irccmd+strlen(irccmd), l-strlen(irccmd), "%q", argv[i]);
	}
	
	passwd = nil;
	servername = nil;
	ARGBEGIN{
	case 'A':
		chattyacme = 1;
		break;
	case 'D':
		debug = 1;
		break;
	case 'P':
		usepass = 1;
		break;
	case 'V':
		chatty = 1;
		break;
	case 'f':
		fullname = EARGF(usage());
		break;
	case 'n':
		if(nnicks < nelem(nicks))
			nicks[nnicks++] = EARGF(usage());
		else
			EARGF(usage());
		break;
	case 's':
		servername = EARGF(usage());
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
	ircwin = w = newwin();
	winname(w, "/irc/%s", servername);
	winctl(w, "dumpdir /");
	winctl(w, "dump %s", irccmd);
	mainfd = winopenfd(w, "body", OWRITE);

	if((efd = winopenfd(w, "errors", OWRITE)) >= 0){
		dup(efd, 1);
		dup(efd, 2);
		if(efd > 2)
			close(efd);
	}

	ircinit();
	c = chancreate(sizeof(void*), 0);
	threadcreate(infothread, c, STACK);
	threadcreate(autowinthread, c, STACK);
	recvp(c);
	recvp(c);
	chanfree(c);
	threadcreate(printmopthread, "mop", STACK);

	if(ircdial(server) < 0)
		sysfatal("dial %s: %r", addr);

	if(login(fullname, nicks, nnicks, passwd) < 0)
		sysfatal("login failed");

	mainwin(w);
}

typedef struct Chat Chat;
struct Chat
{
	char *name;
	Imsg *m;
	Win *w;
	Ichan *ic;
};


enum
{
	XList,
	XChat,
	XDel,
	XDelete,
	XWhois,
	XNick,
	XWho,
	XScroll,
	XNoscroll,
	XXX
};

char *cmds[] = {
	"List",
	"Chat",
	"Del",
	"Delete",
	"Whois",
	"Nick",
	"Who",
	"Scroll",
	"Noscroll",
	nil
};

/*
 * look for s in list
 */
int
lookup(char *s, char **list)
{
	int i;
	
	for(i=0; list[i]; i++)
		if(strcmp(list[i], s) == 0)
			return i;
	return -1;
}

int
doexec(Win *w, Ichan *ic, Event *e)
{
	char buf[512], *cmd, *arg, *p;
	
	if(e->arg)
		snprint(buf, sizeof buf, "%s %s", e->text, e->arg);
	else
		snprint(buf, sizeof buf, "%s", e->text);
	p = buf;
	while(*p && isspace(*p))
		p++;
	cmd = p;
	while(*p && !isspace(*p))
		p++;
	if(*p == 0)
		arg = "";
	else{
		*p++ = 0;
		while(*p && isspace(*p))
			p++;
		arg = p;
	}
	p = arg+strlen(arg);
	while(p > arg && isspace(*(p-1)))
		*--p = 0;
	dprint("%s %s\n", cmd, arg);
	if(cmd[0] == '!'){
		sendp(writechan, esmprint("%s %s", cmd+1, arg));
		return 1;
	}
	switch(lookup(cmd, cmds)){
	case XChat:
		if(*arg == 0 || strpbrk(arg, " \t\r\n")){
			fprint(2, "invalid name %q\n", arg);
			return 1;
		}
		newchat(arg, nil);
		break;
	case XDel:
	case XDelete:
		return 0;
	case XList:
		if(*arg == 0 || strpbrk(arg, " \t\r\n")){
			fprint(2, "invalid name %q\n", arg);
			return 1;
		}
		sendp(writechan, esmprint("LIST :%s", arg));
		break;
	case XWhois:
		if(*arg == 0 || strpbrk(arg, " \t\r\n")){
			fprint(2, "invalid name %q\n", arg);
			return 1;
		}
		sendp(writechan, esmprint("WHOIS :%s", arg));
		break;
	case XNick:
		if(*arg == 0 || strpbrk(arg, " \t\r\n")){
			fprint(2, "invalid name %q\n", arg);
			return 1;
		}
		nick = estrdup(arg);
		sendp(writechan, esmprint("NICK %s", arg));
		break;
	case XWho:
		if(ic==nil)
			goto Default;
		sendp(writechan, esmprint("WHO %s", ic->name));
		break;
	case XScroll:
		winctl(w, "scroll");
		break;
	case XNoscroll:
		winctl(w, "noscroll");
		break;
	default:
	Default:
		winwriteevent(w, e);
		break;
	}
	return 1;
}

void
mainwin(Win *w)
{
	Event *e;
	Channel *c;
	
	winprint(w, "tag", "List Chat ");
	winctl(w, "clean");
	
	c = wineventchan(w);
	while((e = recvp(c)) != nil){
		if(e->c1 != 'M')
			continue;
		switch(e->c2){
		case 'x':
		case 'X':
			if(!doexec(w, nil, e))
				goto out;
			break;
		case 'l':
		case 'L':
			winwriteevent(w, e);
			break;
		}
	}
out:
	windeleteall();
	threadexitsall(nil);
}

uint
fixhostpt(Win *w, uint hostpt)
{
	char buf[10];
	
	buf[0] = '?';
	buf[1] = 0;
	if(hostpt == 0)
		goto fix;
	if(hostpt == 1)
	if(winaddr(w, "#%ud", hostpt-1) < 0
	|| winread(w, "data", buf, sizeof buf) <= 0
	|| buf[0] != '\n')
		goto fix;
	if(hostpt > 1)
	if(winaddr(w, "#%ud", hostpt-2) < 0
	|| winread(w, "data", buf, sizeof buf) <= 0
	|| buf[0] != '\n' || buf[1] != '\n')
		goto fix;
	return hostpt;
	
fix:
	fprint(2, "new newline at %ud - %c\n", hostpt, buf[0]);
	winaddr(w, "#%ud,#%ud", hostpt, hostpt);
	winwrite(w, "data", "\n", 1);
	hostpt++;
	return hostpt;
}

uint
doline(Win *w, char *name, char *line, uint q0, uint q1)
{
	char *p;

	p = line+strlen(line)-1;
	if(p >= line && *p == '\n')
		*p = 0;
	if(strlen(line) > 0){
		sendp(writechan, esmprint("PRIVMSG %s :%s", name, line));
		winaddr(w, "#%ud,#%ud", q0-1, q1);
		winprint(w, "data", "<%s> %s\n\n", nick, line);
		winreadaddr(w, &q1);
	}
	return q1;
}

uint
domsg(Win *w, char *name, uint hostpt)
{
	char *line;
	uint q;

	hostpt = fixhostpt(w, hostpt);
	if(winaddr(w, "#%ud", hostpt) < 0){
		winaddr(w, "$+#0");
		return winreadaddr(w, nil);
	}
	while(winaddr(w, "#%ud", hostpt) >= 0
	&& winaddr(w, "/\\n/") >= 0){
		winreadaddr(w, &q);
		if(q <= hostpt)	/* wrapped */
			break;
		winaddr(w, "#%ud,#%ud", hostpt, q);
		line = winmread(w, "xdata");
		hostpt = doline(w, name, line, hostpt, q);
	}
	return hostpt;
}

int
srcmatch(Isub *s, Imsg *m)
{
	return irccistrcmp(s->aux, m->src) == 0;
}

void
newchat(char *name, Imsg *m)
{
	Chat *ch;
	Ichan *ic;
	Win *w;
	char err[ERRMAX];

	for(w=windows; w; w=w->next){
		if((ch = w->aux) == nil)
			continue;
		if(irccistrcmp(ch->name, name) == 0){
			if(m)
				listput(ch->ic->chatter, m);
			winctl(w, "show");
			return;
		}
	}
		
	w = newwin();
	winname(w, "/irc/%s/%s", servername, name);
	if((ic = ircjoin(name, m!=nil, err)) == nil){
		winprint(w, "body", "join %s: %s\n", name, err);
		winfree(w);
		if(m)
			free(m);
		return;
	}
	
	ch = emalloc(sizeof *ch);
	ch->name = estrdup(name);
	ch->m = m;
	ch->ic = ic;
	ch->w = w;
	w->aux = ch;
	threadcreate(chatwin, ch, STACK);
}

static void
dowho(Ichan *ic)
{
	int i;
	Iwho *w;
	
	fprint(mainfd, "in %s:\n", ic->name);
	for(i=0; i<ic->nwho; i++){
		w = &ic->who[i];
		fprint(mainfd, "\t%s\t%s <%s@%s>\n",
			w->nick, w->fullname, w->user, w->host);
	}
	if(ic->nwho == 0)
		fprint(mainfd, "\t(no one)\n");
}

void
chatwin(void *v)
{
	uint hostpt;
	int lastc2;
	Alt a[3];
	Event *e;
	Ichan *ic;
	Imsg *m;
	List *l;
	Win *w;
	char *name;
	Chat *chat;
	char *file;
	int fd;
	
	chat = v;
	name = chat->name;
	ic = chat->ic;
	w = chat->w;
	
	file = nil;
	fd = -1;

	l = ic->chatter;
	
	hostpt = 1;

	winwrite(w, "tag", "Who ", 4);
	winwrite(w, "body", "\n", 1);

	a[0].op = CHANRCV;
	a[0].v = &m;
	a[0].c = l->rd;
	a[1].op = CHANRCV;
	a[1].v = &e;
	a[1].c = wineventchan(w);
	a[2].op = CHANEND;

	lastc2 = 0;
	if(chat->m){
		m = chat->m;
		goto havemsg;
	}
	
	for(;;){
		switch(alt(a)){
		case 0:
		havemsg:
			dprint("msg %M\n", m);
			hostpt = fixhostpt(w, hostpt);
			winaddr(w, "#%ud", hostpt-1);
			switch(m->cmdnum){
			case RPL_NOTOPIC:
			case RPL_TOPIC:
			case RPL_OWNERTIME:
			case RPL_LIST:
			case ERR_NOCHANMODES:
				break;
			case RPL_WHOISUSER:
			case RPL_WHOWASUSER:
				winprint(w, "data", "<*> %s: %s@%s: %s\n",
					m->arg[0], m->arg[1], m->arg[2], m->arg[4]);
				break;
			case RPL_WHOISSERVER:
				winprint(w, "data", "<*> %s: %s %s\n",
					m->arg[0], m->arg[1], m->arg[2]);
				break;
			case RPL_WHOISOPERATOR:
			case RPL_WHOISCHANNELS:
			case RPL_WHOISIDENTIFIED:
				winprint(w, "data", "<*> %s: %s\n",
					m->arg[0], m->arg[1]);
				break;
			case RPL_WHOISIDLE:
				winprint(w, "data", "<*> %s: %s seconds idle\n",
					m->arg[0], m->arg[1]);
				break;
			case RPL_ENDOFWHO:
				dowho(ic);
				break;
			default:
				if(irccistrcmp(m->cmd, "JOIN") == 0){
					winprint(w, "data", "<*> Who +%s\n",
						m->src);
					break;
				}
				if(irccistrcmp(m->cmd, "PART") == 0){
					winprint(w, "data", "<*> Who -%s\n",
						m->src);
					break;
				}
				if(irccistrcmp(m->cmd, "QUIT") == 0){
					winprint(w, "data", "<*> Who -%s (%s)\n",
						m->src, m->dst);
					break;
				}
				if(irccistrcmp(m->cmd, "NICK") == 0){
					winprint(w, "data", "<*> Who %s => %s\n",
						m->src, m->dst);
					break;
				}
				if(irccistrcmp(m->cmd, "PRIVMSG") == 0
				|| irccistrcmp(m->cmd, "NOTICE") == 0){
					if(irccistrcmp(m->cmd, "PRIVMSG") == 0)
						winprint(w, "data", "<%s> %s\n", m->src, m->arg[0]);
					else
						winprint(w, "data", "[%s] %s\n", m->src, m->arg[0]);
					break;
				}
				winprint(w, "data", "unexpected msg: %M\n", m);
				break;
			}
			winreadaddr(w, &hostpt);
			hostpt++;
			free(m);
			break;
		case 1:
			if(e == nil)
				goto out;	
			/*
			 * F messages are generated only in response to our own
			 * actions, and we know that our own actions always end
			 * at hostpt or hostpt+1, so when we see those
			 * we update hostpt in 
			 * case we've drifted.
			 */
			if(chattyacme || debug)
				print("event %E hostpt %ud\n", e, hostpt);
			if(0)	/* race fix? */
			if(e->c1=='F')
			switch(e->c2){
			case 'I':
				if(lastc2 == 'D'){
					/* something we said */
					hostpt = e->q1;
				}else{
					/* something someone else said */
					hostpt = e->q1+1;
					if(e->q1 == 0)
						hostpt = 0;
				}
				break;
			}
			lastc2 = e->c2;
			if(e->c1=='M' || e->c1=='K')
			switch(e->c2){
			case 'I':
				if(e->q0 < hostpt)
					hostpt += e->q1-e->q0;
				else
					hostpt = domsg(w, name, hostpt);
				break;
			case 'D':
				if(e->q0 < hostpt){
					if(hostpt < e->q1)
						hostpt = e->q0;
					else
						hostpt -= e->q1 - e->q0;
				}
				break;
			case 'x':
			case 'X':
				if(!doexec(w, ic, e))	/* wait for win to close */
					winwriteevent(w, e);	
				break;
			case 'l':
			case 'L':
				winwriteevent(w, e);
				break;
			}
			break;
		}
	}
out:
	dprint("chat %s out\n", name);
	ircleave(ic);
	windel(w, 1);
	winfree(w);
}

int
anyprivmsg(Isub *s, Imsg *m)
{
	USED(s);
	if(!m->src[0] || !m->dst[0])
		return 0;
	return irccistrcmp(m->cmd, "PRIVMSG") == 0
		|| irccistrcmp(m->cmd, "NOTICE") == 0
		|| irccistrcmp(m->cmd, "JOIN") == 0;
}

void
autowinthread(void *v)
{
	Isub sub;
	Imsg *m;
	char *name;
	
	USED(v);

	memset(&sub, 0, sizeof sub);
	sub.match = anyprivmsg;
	sub.ml = listalloc();
	sub.mop = 1;
	sendp(subchan, &sub);
	sendp(v, nil);
	
	while((m = listget(sub.ml)) != nil){
		if(!m->dst[0] || !m->src[0]){
			free(m);
			continue;
		}
		if(nick==nil || irccistrcmp(m->dst, nick) == 0)
			name = m->src;
		else
			name = m->dst;
		if(name[0] == '*'){
			free(m);
			continue;
		}
		newchat(name, m);
	}
}

int
infomatch(Isub *s, Imsg *m)
{
	USED(s);
	
	switch(m->cmdnum){
	case 332:
	case 333:
	case 353:
	case 366:
	case 352:
	case 315:
		return 0;
	default:
		if(m->cmdnum > 0 && m->cmdnum < 400)
			return 1;
	}
	if(irccistrcmp(m->cmd, "NOTICE") == 0 && !m->src[0])
		return 1;
	if(irccistrcmp(m->cmd, "PART") == 0)
		return 1;
	if(irccistrcmp(m->cmd, "QUIT") == 0)
		return 1;
	if(irccistrcmp(m->cmd, "MODE") == 0)
		return 1;
	return 0;
}
	
void
infothread(void *v)
{
	int i;
	Imsg *m;
	Isub sub;
	char *p;
	Fmt fmt;

	memset(&sub, 0, sizeof sub);
	sub.match = infomatch;
	sub.ml = listalloc();
	sendp(subchan, &sub);
	sendp(v, nil);

	while((m = listget(sub.ml)) != nil){
		fmtstrinit(&fmt);
		switch(m->cmdnum){
		default:
			if(m->prefix[0])
				fmtprint(&fmt, ":%s ", m->prefix);
			fmtprint(&fmt, "%s ", m->cmd);
			if(m->dst[0])
				fmtprint(&fmt, "%s ", m->dst);
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 250:
		case 251:
		case 252:
		case 254:
		case 255:
		case 265:
		case 372:
		case 375:
		case 376:
			;
		}
		for(i=0; i<m->narg; i++)
			fmtprint(&fmt, "%s%s", i==0 ? "": " ", m->arg[i]);
		fmtprint(&fmt, "\n");
		p = fmtstrflush(&fmt);
		if(p == nil)
			sysfatal("out of memory");
		winwrite(ircwin, "body", p, strlen(p));
		free(p);
		free(m);
	}
}

