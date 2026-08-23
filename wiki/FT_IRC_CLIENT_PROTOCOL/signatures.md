## COMMANDS
PASS <password>         `PASS secret`
NICK <nickname>         `NICK alice`



USER <user> <mode> <unused> :<realname>

Four parameters, and the fourth is a **trailing** parameter — the colon is
part of the grammar, not decoration. Four rules follow.

**1. Arity and form.** Exactly four parameters, the fourth being the trailing
one. Anything else is `461`. Two malformed shapes fall out of the same check:
`USER a 0 * Real` has four parameters but no trailing, so the realname would
silently become just its first word; `USER a 0 * x :y` has five, so the
trailing is no longer the realname slot. Note also that `<unused>` cannot
begin with `:` — a colon opens the trailing, so `USER u 0 :R :R2` parses as
three parameters, not four.

**2. `<user>` is the RFC 2812 `user` production.**

```
user = 1*( %x01-09 / %x0B-0C / %x0E-1F / %x21-3F / %x41-FF )
```

Any octet except NUL, LF, CR, SPACE and `@`. The first four cannot reach the
handler anyway — framing strips CR/NUL and the tokenizer splits on SPACE — so
`@` is the exclusion that does the work: the prefix stamped on every relayed
line is `nick!user@host`, and a username carrying an `@` makes the prefix
ambiguous about where the host begins. `:` (0x3A) and `!` (0x21) **are**
inside the production and therefore allowed. An over-long username is
truncated to `MAX_USERLEN` rather than refused, and the truncation happens
before validation, so an `@` past the cut is simply gone.

**3. `<mode>` is a bitmask** (RFC 2812 §3.1.3): bit 2 (value `4`) sets user
mode `w`, bit 3 (value `8`) sets user mode `i`. No other bit means anything.
A value that is not a decimal number carries no bits and is *ignored*, not
refused — the RFC says "should be a numeric", and refusing a registration
over a cosmetic field would be the worse failure.

```
USER u 0  * :R   ->  no user modes      USER u 12 * :R   ->  +iw
USER u 4  * :R   ->  +w                 USER u 15 * :R   ->  +iw  (bits 0,1 ignored)
USER u 8  * :R   ->  +i                 USER u abc * :R  ->  no user modes
```

**4. `<unused>` has no meaning.** Clients send `*`; anything is accepted. It
simply has to be present, because it holds the realname's position.

`<realname>` is stored verbatim, whatever it contains — spaces, colons,
punctuation, leading and trailing whitespace, or nothing at all. The one
thing it cannot contain is CR or LF, because those *terminate* the message:
`USER u 0 * :real<CR>JOIN #x` is two lines, and the realname is `real`.

The full matrix — every parameter axis crossed with every other — is
exercised in `tests/test_user.cpp`.

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
MODE <target> <modes> [parameters...]

The `<modes>` part has a shape of its own, and two rules govern it.

**1. It must open with a sign.** `+i`, `-o`, `-o+i-t` are mode strings;
`i`, `it`, `o alice` are not. There is no implicit `+`, so a string that does
not open with `+` or `-` applies nothing and is answered with nothing — not
even `472 ERR_UNKNOWNMODE`, because its characters were never mode characters.
The authorisation replies (`403`, `442`, `482`) still come first: they answer
whether you may touch the target's modes at all, which is true or false
whatever the string turned out to say.

**2. Past the sign, modes cumulate and the sign may flip.** Each letter is
applied in the order written, under the sign in force at that point:

```
+ii         apply i twice          -oo alice bob   revoke both
+it         two flags, one sign    -oi alice       mixed, one sign
-o+i-t bob  the sign flips twice   +ikl key 5      i, then key, then limit
+-+-i       a run of signs collapses to the last one before the letter
+           a lone sign applies nothing
```

Each parameter-taking letter draws the **next** positional parameter, in the
order the letters appear — so `+ko key alice` and `+ok alice key` differ only
in which parameter lands where. Of the five implemented modes, `o` always
takes one, `k` and `l` take one when adding, and `i`/`t` never do. `-k` takes
the key only when the modes still to come do not need it, so `-k+o alice`
gives `alice` to `+o` rather than eating it as a key.

The echo restates the sign only where it actually changes, so `+o-i-o a b`
comes back as `+o-io a b`. A mode string dense enough that its echo would
exceed 512 octets is split across several `MODE` lines rather than truncated.

Errors are reported once per **distinct** complaint, not per occurrence:
`+ooo` with no parameters is one `461`, and `+jfsadfsahf` is six `472`s (for
`j f s a d h`), not ten.

The full matrix is exercised in `tests/test_modes.cpp` and `tests/08_modes.sh`.
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