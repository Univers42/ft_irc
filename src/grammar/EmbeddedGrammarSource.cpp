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

    "cap-cmd    =  \"CAP\" [ SPACE $capsub [ SPACE trailing ] ]\n"
    "pass-cmd   =  \"PASS\" SPACE [ \":\" ] $password\n"
    "nick-cmd   =  \"NICK\" SPACE [ \":\" ] $newnick\n"
    "user-cmd   =  \"USER\" SPACE $username SPACE $usermode SPACE $unused"
    " SPACE \":\" $realname\n"
    "quit-cmd   =  \"QUIT\" [ SPACE [ \":\" ] $quitmsg ]\n"
    "pong-cmd   =  \"PONG\" [ SPACE [ \":\" ] $pingtoken ]\n"

    "ping-cmd   =  \"PING\" [ SPACE [ \":\" ] $pingtoken ]\n"
    "join-cmd   =  \"JOIN\" SPACE ( \"0\" / $chanlist [ SPACE $keylist ] )\n"
    "part-cmd   =  \"PART\" SPACE $chanlist [ SPACE [ \":\" ] $partmsg ]\n"
    "privmsg-cmd = \"PRIVMSG\" SPACE $msgtarget SPACE [ \":\" ] $msgtext\n"
    "notice-cmd =  \"NOTICE\" SPACE $msgtarget SPACE [ \":\" ] $msgtext\n"
    "kick-cmd   =  \"KICK\" SPACE $kickchans SPACE $kickusers"
    " [ SPACE [ \":\" ] $kickreason ]\n"
    "invite-cmd =  \"INVITE\" SPACE $invnick SPACE $invchan\n"

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

}  // namespace Abnf
