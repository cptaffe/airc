<$PLAN9/src/mkhdr

TARG=Irc ircmux

OFILES=\
	irc.$O\
	list.$O\

HFILES=irc.h

<$PLAN9/src/mkmany

$O.Irc: acme.$O airc.$O
acme.$O airc.$O: acme.h

tgz:V:
	tar czf irc.tar.gz COPYRIGHT \
		acme.[ch] irc.[ch] airc.c list.c Irc.c ircmux.c mkfile README

push:V:
	mk tgz
	scp irc.tar.gz swtch.com:www/swtch.com
	ssh swtch.com 'cd www/swtch.com/irc && tar xzvf ../irc.tar.gz && 9 mk -f mkfile2'

xpush:V:
	scp irc.tar.gz swtch.com:www/swtch.com/xirc.tar.gz

