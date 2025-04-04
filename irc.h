enum
{
	MAXARG = 16,

	/* IRC response codes */
	RPL_WELCOME = 001,
	RPL_YOURHOST = 002,
	RPL_CREATED = 003,
	RPL_MYINFO = 004,
	RPL_BOUNCE = 005,
	RPL_USERHOST = 302,
	RPL_ISON = 303,
	RPL_AWAY = 301,
	RPL_UNAWAY = 305,
	RPL_NOWAWAY = 306,
	RPL_WHOISUSER = 311,
	RPL_WHOISSERVER = 312,
	RPL_WHOISOPERATOR = 313,
	RPL_WHOISIDLE = 317,
	RPL_ENDOFWHOIS = 318,
	RPL_WHOISCHANNELS = 319,
	RPL_WHOISIDENTIFIED = 320,
	RPL_WHOWASUSER = 314,
	RPL_ENDOFWHOWAS = 369,
	RPL_LISTSTART = 321,
	RPL_LIST = 322,
	RPL_LISTEND = 323,
	RPL_UNIQOPIS = 325,
	RPL_CHANNELMODEIS = 324,
	RPL_CREATIONTIME = 329,
	RPL_NOTOPIC = 331,
	RPL_TOPIC = 332,
	RPL_OWNERTIME = 333,
	RPL_INVITING = 341,
	RPL_SUMMONING = 342,
	RPL_INVITELIST = 346,
	RPL_ENDOFINVITELIST = 347,
	RPL_EXCEPTLIST = 348,
	RPL_ENDOFEXCEPTLIST = 349,
	RPL_VERSION = 351,
	RPL_WHOREPLY = 352,
	RPL_ENDOFWHO = 315,
	RPL_NAMREPLY = 353,
	RPL_ENDOFNAMES = 366,
	RPL_LINKS = 364,
	RPL_ENDOFLINKS = 365,
	RPL_BANLIST = 367,
	RPL_ENDOFBANLIST = 368,
	RPL_INFO = 371,
	RPL_ENDOFINFO = 374,
	RPL_MOTDSTART = 375,
	RPL_MOTD = 372,
	RPL_ENDOFMOTD = 376,
	RPL_YOUREOPER = 381,
	RPL_REHASHING = 382,
	RPL_YOURESERVICE = 383,
	RPL_TIME = 391,
	RPL_USERSSTART = 392,
	RPL_USERS = 393,
	RPL_ENDOFUSERS = 394,
	RPL_NOUSERS = 395,
	RPL_TRACELINK = 200,
	RPL_TRACECONNECTING = 201,
	RPL_TRACEHANDSHAKE = 202,
	RPL_TRACEUNKNOWN = 203,
	RPL_TRACEOPERATOR = 204,
	RPL_TRACEUSER = 205,
	RPL_TRACESERVER = 206,
	RPL_TRACESERVICE = 207,
	RPL_TRACENEWTYPE = 208,
	RPL_TRACECLASS = 209,
	RPL_TRACERECONNECT = 210,
	RPL_TRACELOG = 261,
	RPL_TRACEEND = 262,
	RPL_STATSLINKINFO = 211,
	RPL_STATSCOMMANDS = 212,
	RPL_ENDOFSTATS = 219,
	RPL_STATSUPTIME = 242,
	RPL_STATSOLINE = 243,
	RPL_UMODEIS = 221,
	RPL_SERVLIST = 234,
	RPL_SERVLISTEND = 235,
	RPL_LUSERCLIENT = 251,
	RPL_LUSEROP = 252,
	RPL_LUSERUNKNOWN = 253,
	RPL_LUSERCHANNELS = 254,
	RPL_LUSERME = 255,
	RPL_ADMINME = 256,
	RPL_ADMINLOC1 = 257,
	RPL_ADMINLOC2 = 258,
	RPL_ADMINEMAIL = 259,
	RPL_TRYAGAIN = 263,
	ERR_NOSUCHNICK = 401,
	ERR_NOSUCHSERVER = 402,
	ERR_NOSUCHCHANNEL = 403,
	ERR_CANNOTSENDTOCHAN = 404,
	ERR_TOOMANYCHANNELS = 405,
	ERR_WASNOSUCHNICK = 406,
	ERR_TOOMANYTARGETS = 407,
	ERR_NOSUCHSERVICE = 408,
	ERR_NOORIGIN = 409,
	ERR_NORECIPIENT = 411,
	ERR_NOTEXTTOSEND = 412,
	ERR_NOTOPLEVEL = 413,
	ERR_WILDTOPLEVEL = 414,
	ERR_BADMASK = 415,
	ERR_UNKNOWNCOMMAND = 421,
	ERR_NOMOTD = 422,
	ERR_NOADMININFO = 423,
	ERR_FILEERROR = 424,
	ERR_NONICKNAMEGIVEN = 431,
	ERR_ERRONEUSNICKNAME = 432,
	ERR_NICKNAMEINUSE = 433,
	ERR_NICKCOLLISION = 436,
	ERR_UNAVAILRESOURCE = 437,
	ERR_USERNOTINCHANNEL = 441,
	ERR_NOTONCHANNEL = 442,
	ERR_USERONCHANNEL = 443,
	ERR_NOLOGIN = 444,
	ERR_SUMMONDISABLED = 445,
	ERR_USERSDISABLED = 446,
	ERR_NOTREGISTERED = 451,
	ERR_NEEDMOREPARAMS = 461,
	ERR_ALREADYREGISTRED = 462,
	ERR_NOPERMFORHOST = 463,
	ERR_PASSWDMISMATCH = 464,
	ERR_YOUREBANNEDCREEP = 465,
	ERR_YOUWILLBEBANNED = 466,
	ERR_KEYSET = 467,
	ERR_CHANNELISFULL = 471,
	ERR_UNKNOWNMODE = 472,
	ERR_INVITEONLYCHAN = 473,
	ERR_BANNEDFROMCHAN = 474,
	ERR_BADCHANNELKEY = 475,
	ERR_BADCHANMASK = 476,
	ERR_NOCHANMODES = 477,
	ERR_BANLISTFULL = 478,
	ERR_NOPRIVILEGES = 481,
	ERR_CHANOPRIVSNEEDED = 482,
	ERR_CANTKILLSERVER = 483,
	ERR_RESTRICTED = 484,
	ERR_UNIQOPPRIVSNEEDED = 485,
	ERR_NOOPERHOST = 491,
	ERR_UMODEUNKNOWNFLAG = 501,
	ERR_USERSDONTMATCH = 502,
};

/* a single IRC msg -- :foo!bar@baz 372 gnot greeting :hello world */
typedef struct Imsg Imsg;
struct Imsg
{
	int len;			/* sizeof(Imsg)+space for strings */
	char *raw;		/* raw message */
	char *prefix;		/* :foo!bar@baz */
	char *src;			/* foo */
	char *dst;			/* gnot */
	char *cmd;		/* 372 */
	int cmdnum;
	char *arg[MAXARG];	/* greeting, 'hello world' */
	int narg;
};
#pragma varargck type "M" Imsg*

/* like a Channel, but arbitrarily large buffering and only holds pointers */
typedef struct List List;
struct List
{
	Channel *wr;
	Channel *rd;
	Channel *die;
	void *head;
	void *tail;
};

List*		listalloc(void);
void		listfree(List*);
void*	listget(List*);
void		listput(List*, void*);

/* an IRC subscription */
typedef struct Isub Isub;
struct Isub
{
	void *aux;
	int mop;		/* receive messages no one else wants */
	int snoop;		/* receive all messages */
	int die;		/* get a nil when the connection dies */
	int (*match)(Isub*, Imsg*);
	List *ml;		/* list(Imsg*) */
	int disable;
};

typedef struct Iwho Iwho;
struct Iwho
{
	char *user;
	char *host;
	char *server;
	char *nick;
	char mode[5];
	int hops;
	char *fullname;
};

/* an IRC channel */
typedef struct Ichan Ichan;
struct Ichan
{
	int ref;
	int sure;
	char *name;
	Isub sub;
	char *topic;
	char *owner;
	ulong time;
	Iwho *who;
	int nwho;
	int _inwho;
	List *chatter;
	
	char err[ERRMAX];
	QLock q;	/* holds back clients while establishing connection */
};

extern	int	chatty;
extern	int	debug;
extern	int	usepass;
extern	char*	nick;
extern	Channel	*readchan;
extern	Channel	*writechan;
extern	Channel	*subchan;
extern	Channel	*unsubchan;


void		addname(Ichan *ic, char *p);
Imsg*		copymsg(Imsg *i);
int		ichanmatch(Isub *s, Imsg *m);
void		ichanthread(void *v);
int		imsgfmt(Fmt *fmt);
int		inchannel(char *who, Ichan *ic);
void		inputrelay(void *v);
int		irccistrcmp(char *s, char *t);
void		ircinit(void);
Ichan*		ircjoin(char *name, int, char e[ERRMAX]);
void		ircleave(Ichan*);
void		ircread(void *v);
int		irctokenize(char *s, char **args, int maxargs);
void		ircwrite(void *v);
int		login(char *fullname, char **nicks, int nnicks, char *passwd);
void		muxthread(void *v);
int		pingmatch(Isub *i, Imsg *m);
void		pingthread(void *v);
void		printmopthread(void *v);
int		ircdial(char*);
Imsg*	ircparsemsg(char*);
char*	ircstrlwr(char*);

enum
{
	STACK = 32768
};

#define dprint if(debug) print

