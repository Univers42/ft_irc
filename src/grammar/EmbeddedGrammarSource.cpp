#include "grammar/EmbeddedGrammarSource.hpp"

#include <string>

namespace Abnf {
namespace {
const char kGrammar[] =

    "SPACE      =  %x20\n"
    "nospcrlfcl =  %x01-09 / %x0B-0C / %x0E-1F / %x21-39 / %x3B-FF\n"
    "middle     =  nospcrlfcl *( \":\" / nospcrlfcl )\n"
    "trailing   =  *( \":\" / \" \" / nospcrlfcl )\n"
    "letter     =  %x41-5A / %x61-7A\n"
    "digit      =  %x30-39\n"
    "special    =  %x5B-60 / %x7B-7D\n"

    "nickname   =  ( letter / special )"
    " *8( letter / digit / special / \"-\" )\n"

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

    "cap-cmd    =  \"CAP\""
    " [ SPACE $capsub [ SPACE [ \":\" ] $capparam ] ] *SPACE\n"
    "pass-cmd   =  \"PASS\" SPACE [ \":\" ] $password *SPACE\n"
    "nick-cmd   =  \"NICK\" SPACE [ \":\" ] $newnick *SPACE\n"
    "user-cmd   =  \"USER\" SPACE $username SPACE $usermode SPACE $unused"
    " SPACE \":\" $realname *SPACE\n"
    "quit-cmd   =  \"QUIT\" [ SPACE [ \":\" ] $quitmsg ] *SPACE\n"
    "pong-cmd   =  \"PONG\" [ SPACE [ \":\" ] $pingtoken ] *SPACE\n"

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

    "topic-cmd  =  \"TOPIC\" SPACE $topicchan"
    " [ SPACE \":\" $topictext ] *SPACE\n"
    "mode-cmd   =  \"MODE\" SPACE $modetarget"
    " [ SPACE $modestring *13( SPACE $modeparam ) ] *SPACE\n"
    "who-cmd    =  \"WHO\" [ SPACE $whomask ] *SPACE\n"
    "whois-cmd  =  \"WHOIS\" SPACE [ middle SPACE ]"
    " $whoisnick *SPACE\n"
    "userhost-cmd = \"USERHOST\" SPACE $hostnick"
    " *4( SPACE $hostnick ) *SPACE\n"

    "command    =  1*letter / 3digit\n"
    "param      =  middle\n"
    "trail      =  trailing\n"
    "prefix     =  middle\n"
    "sp         =  1*SPACE\n"
    "params     =  *14( sp $param ) [ sp \":\" $trail ]\n"
    "           =/ 14( sp $param ) [ sp [ \":\" ] $trail ]\n"
    "message    =  *SPACE [ \":\" $prefix sp ] $command [ params ] *SPACE\n";

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

}  // namespace Abnf
