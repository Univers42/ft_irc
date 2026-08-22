## COMMANDS
PASS <password>         `PASS secret`
NICK <nickname>         `NICK alice`



USER <user> <mode> <unused> <username[:([^\r\n]*)]> # meaning that hte  username has a format to respect
example:

```bash
USER Alice 0 * :Alice	        ✅	Normal
USER Alice 0 * :Alice Smith	    ✅	Space in realname
USER Alice 0 * :Alice Smith Jr.	✅	Multiple spaces/words
USER Alice 0 * :                ✅	Empty realname
USER Alice 0 * :                ✅	Realname is one space
USER Alice 0 * :                ✅	Realname contains spaces
USER Alice 0 * :Alice:Smith	    ✅	: allowed in trailing
USER Alice 0 * :Alice * Smith	✅	* allowed
USER Alice 0 * :Alice @ home	✅	Spaces/punctuation allowed
USER Alice 0 * :123	            ✅	Fine
USER Alice 0 * :!@#$%^&*()      ✅	Generally valid trailing chars
USER Alice 0 * :Alice\tSmith    ⚠️	TAB is not a normal space and depends on grammar/parser
USER Alice 0 * :Alice\r          ❌	CR terminates IRC message
USER Alice 0 * :Alice\n          ❌	LF terminates IRC message
USER Alice 0 * :Alice\r\n	     ✅	CRLF terminates the message
USER Alice 0 * Alice             ❌	Missing trailing-parameter syntax according to the RFC form
USER Alice 0 *                   ❌	Missing realname
USER Alice 0                     ❌	Missing parameters
USER Alice                       ❌	Missing parameters
USER                             ❌	Missing parameters
```



JOIN <channel>[,<channel>...][<key>[,<key>...]] / 0
TOPIC <channel>[<topic>]
INVITE <nickname><channel>
KICK <channel>[,...]<user>[,...][<comment>]
MODE <target><modes>[parameters...]
1PRIVMSG <msgrarget> <text>
QUIT <message>

message     =  [":" prefix SPACE] command [ params ] crlf
prefix      =   servername / ( nickname [ [ "!" user ] "@" host ])
command     =   1*letter / 3digit
params      = *14( SPACE middle ) [SPACE ":" trailing ]
            =/ 14( SPACE middle ) [ SPACE [ ":" ] trailing ]
nospcrlfcl  = %x01-09 /  %x0B-0C / %x0E-1F / %x21-39 / %x3B-FF
middle      = nospcrlfcl *( ":" / nospcrlfcl )
trailing    = *(":" / " " / nospcrlfcl)
SPACE       = %x20
crlf        = %x0D %x0A



[:prefix ] COMMAND [parameers]\r\n

for example 

```bash
PASS secret\r\n
NICK alice\r\n
USER alice 0 * :Alice smith\r\n
JOIN #42 \r\n
PRIVMSG #42 :hello everyone\r\n
```