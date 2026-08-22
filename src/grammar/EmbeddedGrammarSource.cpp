#include "grammar/EmbeddedGrammarSource.hpp"

namespace {

/* ── The wire grammar ──────────────────────────────────────────────────────
**
** RFC 2812 §2.3.1 followed by one production per command this server answers.
** It is compiled at startup, and every inbound line is matched against the
** production for its command -- which is where the line gets cut into named
** fields.
**
**
** THE ONE RULE TO UNDERSTAND BEFORE EDITING THIS
**
** The grammar decides SHAPE. The handler decides POLICY.
**
** A production that fails to match produces exactly one outcome -- "malformed"
** -- and nothing more specific. So any field whose CONTENT needs its own
** numeric reply must be captured permissively here and judged in the handler:
**
**   NICK  `newnick` is a plain `middle`, NOT the RFC `nickname` production.
**         An over-long nick is truncated to NICKLEN and a malformed one
**         answers 432, and neither is expressible as a non-match. Putting
**         `nickname` here would turn `NICK abcdefghij` into a generic parse
**         failure and silently break the truncation HexChat depends on.
**
**   JOIN  `chanlist` is one `middle`, comma-split by the handler, because
**         `JOIN #ok,#bad` must join the good one and answer 476 for the
**         other. A whole-line reject cannot do that.
**
**   MODE  the mode string and its arguments stay whole; the sign walker in
**         CommandOperator.cpp already handles them and reports per-flag
**         errors a grammar cannot express.
**
** USER is the deliberate exception. Its ":" is written into the production, so
** `USER a 0 * Alice` simply is not a USER line. That is the strict-colon
** decision, enforced here rather than by an if-statement in cmdUser.
*/
const char kGrammar[] =
    /* ─── RFC 2812 §2.3.1 ─── */
    "SPACE      =  %x20\n"
    "nospcrlfcl =  %x01-09 / %x0B-0C / %x0E-1F / %x21-39 / %x3B-FF\n"
    "middle     =  nospcrlfcl *( \":\" / nospcrlfcl )\n"
    "trailing   =  *( \":\" / \" \" / nospcrlfcl )\n"
    "letter     =  %x41-5A / %x61-7A\n"
    "digit      =  %x30-39\n"
    "special    =  %x5B-60 / %x7B-7D\n"
    /* Kept for the name-grammar probes even though NICK deliberately does not
    ** use it -- see the note above. */
    "nickname   =  ( letter / special )"
    " *8( letter / digit / special / \"-\" )\n"

    /* ─── Field names: these are what `$` captures produce ─── */
    "password   =  middle\n"
    "newnick    =  middle\n"
    "username   =  middle\n"
    "usermode   =  middle\n"
    "unused     =  middle\n"
    "realname   =  trailing\n"
    "quitmsg    =  trailing\n"
    "capsub     =  middle\n"
    "pingtoken  =  trailing\n"
    "chanlist   =  middle\n"
    "keylist    =  middle\n"
    "partmsg    =  trailing\n"
    "msgtarget  =  middle\n"
    "msgtext    =  trailing\n"
    "kickchans  =  middle\n"
    "kickusers  =  middle\n"
    "kickreason =  trailing\n"
    "invnick    =  middle\n"
    "invchan    =  middle\n"
    "topicchan  =  middle\n"
    "topictext  =  trailing\n"
    "modetarget =  middle\n"
    "modestring =  middle\n"
    "modeargs   =  trailing\n"
    "whomask    =  middle\n"
    "whoisnick  =  middle\n"
    "hostnicks  =  trailing\n"

    /* ─── Accepted before registration ─── */
    "cap-cmd    =  \"CAP\" [ SPACE $capsub [ SPACE trailing ] ]\n"
    "pass-cmd   =  \"PASS\" SPACE [ \":\" ] $password\n"
    "nick-cmd   =  \"NICK\" SPACE [ \":\" ] $newnick\n"
    "user-cmd   =  \"USER\" SPACE $username SPACE $usermode SPACE $unused"
    " SPACE \":\" $realname\n"
    "quit-cmd   =  \"QUIT\" [ SPACE [ \":\" ] $quitmsg ]\n"
    "pong-cmd   =  \"PONG\" [ SPACE [ \":\" ] $pingtoken ]\n"

    /* ─── Require registration ─── */
    "ping-cmd   =  \"PING\" [ SPACE [ \":\" ] $pingtoken ]\n"
    "join-cmd   =  \"JOIN\" SPACE ( \"0\" / $chanlist [ SPACE $keylist ] )\n"
    "part-cmd   =  \"PART\" SPACE $chanlist [ SPACE [ \":\" ] $partmsg ]\n"
    "privmsg-cmd = \"PRIVMSG\" SPACE $msgtarget SPACE [ \":\" ] $msgtext\n"
    "notice-cmd =  \"NOTICE\" SPACE $msgtarget SPACE [ \":\" ] $msgtext\n"
    "kick-cmd   =  \"KICK\" SPACE $kickchans SPACE $kickusers"
    " [ SPACE [ \":\" ] $kickreason ]\n"
    "invite-cmd =  \"INVITE\" SPACE $invnick SPACE $invchan\n"
    /* No topic argument is a query; `TOPIC #c :` with an empty trailing is a
    ** clear. Different lines, and the capture tells them apart. */
    "topic-cmd  =  \"TOPIC\" SPACE $topicchan [ SPACE \":\" $topictext ]\n"
    "mode-cmd   =  \"MODE\" SPACE $modetarget"
    " [ SPACE $modestring [ SPACE $modeargs ] ]\n"
    "who-cmd    =  \"WHO\" [ SPACE $whomask ]\n"
    "whois-cmd  =  \"WHOIS\" SPACE [ middle SPACE ] $whoisnick\n"
    "userhost-cmd = \"USERHOST\" SPACE $hostnicks\n";

}  // namespace

EmbeddedGrammarSource::EmbeddedGrammarSource() {}

EmbeddedGrammarSource::~EmbeddedGrammarSource() {}

const char* EmbeddedGrammarSource::origin() const {
  return "<embedded RFC 2812 grammar>";
}

bool EmbeddedGrammarSource::read(std::string& out) const {
  out.assign(kGrammar);
  return true;
}
