#include "grammar/EmbeddedGrammarSource.hpp"

#include <string>

/*
** The RFC 2812 grammar, as one string literal. This is why the server needs no
** data files: FT_IRC_GRAMMAR can override it, but the default is right here.
**
** It reads bottom-up in four layers, each built only from the one below it:
**
**   1. byte classes      raw %x ranges -- the only place octets are named
**   2. parameter aliases one name per protocol field, all aliasing middle or
**                        trailing. They exist so a capture has a MEANINGFUL
**                        name: $chanlist and $msgtext are both `middle`, but
**                        the handler reads them by name, not by shape.
**   3. command rules     one "<name>-cmd" per command, with $captures marking
**                        the spans a handler wants. Server::bindCommandRules()
**                        finds these by the "-cmd" suffix.
**   4. generic message   the fallback used when no command rule matched
**
** Two conventions run through the command rules and are worth stating once:
**   - Every one ends in *SPACE, absorbing trailing whitespace so a client that
**     pads its lines still parses.
**   - [ ":" ] before a final capture makes the RFC's colon prefix optional,
**     which is what real clients actually send.
*/
namespace Abnf {
namespace {
const char kGrammar[] =
    //< 1. Byte classes. The only rules that mention raw octets; everything
    //<    below is built out of these. nospcrlfcl is "any byte except NUL,
    //<    CR, LF, space or ':'" -- the RFC's core parameter alphabet.
    "SPACE      =  %x20\n"
    "nospcrlfcl =  %x01-09 / %x0B-0C / %x0E-1F / %x21-39 / %x3B-FF\n"
    "middle     =  nospcrlfcl *( \":\" / nospcrlfcl )\n"
    "trailing   =  *( \":\" / \" \" / nospcrlfcl )\n"
    "letter     =  %x41-5A / %x61-7A\n"
    "digit      =  %x30-39\n"
    "special    =  %x5B-60 / %x7B-7D\n"

    //< Nickname is the one field with real structure: a leading letter or
    //< special, then up to 8 more. Bounded, so the compiled strategy unrolls it.
    "nickname   =  ( letter / special )"
    " *8( letter / digit / special / \"-\" )\n"

    //< 2. Parameter aliases. Every one of these is just `middle` or `trailing`;
    //<    the name is the entire point. A capture called $kickreason tells a
    //<    handler what it is holding in a way `trailing` never could.
    "password   =  middle\n"
    "newnick    =  middle\n"
    "username   =  middle\n"
    "usermode   =  middle\n"
    "unused     =  middle\n"
    "realname   =  trailing\n"
    "quitmsg    =  trailing\n"
    "capsub     =  middle\n"
    "capparam   =  trailing\n"
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
    "modeparam  =  middle\n"
    "whomask    =  middle\n"
    "whoisnick  =  middle\n"
    "hostnick   =  middle\n"

    //< 3. Command rules, one per command, named "<command>-cmd" because that
    //<    suffix is what Server::bindCommandRules() looks for. The $ prefixes
    //<    are this module's extension, not RFC syntax: each marks a span to
    //<    hand back in the MatchResult.
    //<    Registration and session commands first.
    "cap-cmd    =  \"CAP\""
    " [ SPACE $capsub [ SPACE [ \":\" ] $capparam ] ] *SPACE\n"
    "pass-cmd   =  \"PASS\" SPACE [ \":\" ] $password *SPACE\n"
    "nick-cmd   =  \"NICK\" SPACE [ \":\" ] $newnick *SPACE\n"
    "user-cmd   =  \"USER\" SPACE $username SPACE $usermode SPACE $unused"
    " SPACE \":\" $realname *SPACE\n"
    "quit-cmd   =  \"QUIT\" [ SPACE [ \":\" ] $quitmsg ] *SPACE\n"
    "pong-cmd   =  \"PONG\" [ SPACE [ \":\" ] $pingtoken ] *SPACE\n"

    //< Channel and messaging commands.
    "ping-cmd   =  \"PING\" [ SPACE [ \":\" ] $pingtoken ] *SPACE\n"
    "join-cmd   =  \"JOIN\" SPACE"
    " ( \"0\" / $chanlist [ SPACE $keylist ] ) *SPACE\n"
    "part-cmd   =  \"PART\" SPACE $chanlist"
    " [ SPACE [ \":\" ] $partmsg ] *SPACE\n"
    "privmsg-cmd = \"PRIVMSG\" SPACE $msgtarget"
    " SPACE [ \":\" ] $msgtext *SPACE\n"
    "notice-cmd =  \"NOTICE\" SPACE $msgtarget"
    " SPACE [ \":\" ] $msgtext *SPACE\n"
    "kick-cmd   =  \"KICK\" SPACE $kickchans SPACE $kickusers"
    " [ SPACE [ \":\" ] $kickreason ] *SPACE\n"
    "invite-cmd =  \"INVITE\" SPACE $invnick SPACE $invchan *SPACE\n"

    //< Channel management and queries. Note MODE's *13( SPACE $modeparam ):
    //< the bound is explicit rather than a bare *, because an UNBOUNDED
    //< repetition containing a capture is exactly what ProgramCompiler has to
    //< refuse -- a loop reuses one slot pair, so only the last param would
    //< survive. Bounded means it unrolls, and every param keeps its own slot.
    "topic-cmd  =  \"TOPIC\" SPACE $topicchan"
    " [ SPACE [ \":\" ] $topictext ] *SPACE\n"
    "mode-cmd   =  \"MODE\" SPACE $modetarget"
    " [ SPACE $modestring *13( SPACE $modeparam ) ] *SPACE\n"
    "names-cmd  =  \"NAMES\" [ SPACE $chanlist ] *SPACE\n"
    "who-cmd    =  \"WHO\" [ SPACE $whomask ] *SPACE\n"
    "whois-cmd  =  \"WHOIS\" SPACE [ middle SPACE ]"
    " $whoisnick *SPACE\n"
    "userhost-cmd = \"USERHOST\" SPACE $hostnick"
    " *4( SPACE $hostnick ) *SPACE\n"

    //< 4. The generic message form, used by Server::_messageRule whenever no
    //<    specific command rule matched. This is what lets an unknown command
    //<    still parse into a command name plus parameters, so the server can
    //<    answer ERR_UNKNOWNCOMMAND instead of dropping the line.
    "command    =  1*letter / 3digit\n"
    "param      =  middle\n"
    "trail      =  trailing\n"
    "prefix     =  middle\n"
    "sp         =  1*SPACE\n"
    //< RFC 2812 caps a message at 15 parameters. The "=/" line is incremental
    //< alternation (@see GrammarBuilder::parseRule): the second form covers the
    //< full-14 case, where the 15th parameter needs no ':' to be recognised.
    "params     =  *14( sp $param ) [ sp \":\" $trail ]\n"
    "           =/ 14( sp $param ) [ sp [ \":\" ] $trail ]\n"
    "message    =  *SPACE [ \":\" $prefix sp ] $command [ params ] *SPACE\n";

}  // namespace

EmbeddedGrammarSource::EmbeddedGrammarSource() {}

EmbeddedGrammarSource::~EmbeddedGrammarSource() {}

const char* EmbeddedGrammarSource::origin() const { return "<embedded RFC 2812 grammar>"; }

//< Cannot fail: the text is a string literal in this translation unit.
bool EmbeddedGrammarSource::read(std::string& out) const {
  out.assign(kGrammar);
  return true;
}

}  // namespace Abnf
