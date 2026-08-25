#include "grammar/AbnfLineReader.hpp"

#include <sstream>
#include <string>
#include <vector>

#include "grammar/AbnfChars.hpp"

/*
** Two jobs, both about physical layout rather than grammar meaning, and both
** done before anything is parsed:
**
**   stripComment()  cuts the line at a ';', but tracks inQuote first, so the
**                   ';' inside "a;b" survives as data.
**   read()          folds continuation lines. An INDENTED line is glued onto
**                   the one above (pending.text += " " + body), and a line
**                   starting with a bare "=/" gets lastRuleName prepended so
**                   it reads as a complete rule again.
**
** What comes out is a vector<Line>: one complete rule per string, each tagged
** with the physical line it started on so later errors can point at real
** source. GrammarBuilder therefore never has to think about newlines at all.
**
** The only error this file can raise is a leading "=/" with no rule above it
** to attach to. Everything else is syntax, and syntax is GrammarBuilder's job.
*/
namespace Abnf {
AbnfLineReader::AbnfLineReader() : _errorLine(0) {}

AbnfLineReader::~AbnfLineReader() {}

const std::string& AbnfLineReader::error() const { return _error; }

std::size_t AbnfLineReader::errorLine() const { return _errorLine; }

std::string AbnfLineReader::stripComment(const std::string& raw) {
  std::string out(raw);
  bool inQuote = false;

  for (std::string::size_type i = 0; i < out.size(); ++i) {
    if (out[i] == '"') {  //< quote toggle · a ';' inside "a;b" is DATA, not a comment
      inQuote = !inQuote;
    } else if (out[i] == ';' && !inQuote) {  //< "SPACE = %x20  ; space" -> cut at ';'
      out.erase(i);
      break;
    }
  }
  return out;
}

bool AbnfLineReader::read(const std::string& text, std::vector<Line>& out) {
  _error.clear();
  _errorLine = 0;
  out.clear();

  std::istringstream in(text);
  std::string raw;
  std::size_t physical = 0;

  Line pending;  //< the rule being accumulated; empty means "none yet"
  pending.number = 0;
  std::string lastRuleName;  //< owner for a later bare "=/"; see the redefines branch

  while (std::getline(in, raw)) {
    ++physical;

    if (!raw.empty() && raw[raw.size() - 1] == '\r') raw.erase(raw.size() - 1);  //< CRLF-saved .abnf file
    raw = stripComment(raw);

    const std::string body = AbnfChars::trimmed(raw);
    if (body.empty()) continue;  //< blank line, or a line that was ONLY a comment: "; header"

    const bool continuation = AbnfChars::isBlank(raw[0]);  //< indented · "      *8( letter )" folds up
    const bool redefines = (body[0] == '=');               //< RFC's elided form · "           =/ 14( SPACE middle )"

    if (continuation && !redefines && !pending.text.empty()) {  //< plain fold · NOT the elided "=/" case
      pending.text += " ";
      pending.text += body;
      continue;
    }

    //< Not a continuation, so whatever was being accumulated is finished.
    if (!pending.text.empty()) {  //< a new rule starts here, so flush the one being built
      out.push_back(pending);
      pending.text.clear();
    }

    if (redefines) {
      if (lastRuleName.empty()) {  //< "=/ x" as the FIRST line · nothing to attach it to -> error
        _errorLine = physical;
        _error = "'=' continuation with no rule name above it";
        return false;
      }
      //< RFC 5234's elided form: "=/ x" means "<previous rule> =/ x". Splicing
      //< the name back in here is what lets parseRule() see one uniform shape.
      pending.text = lastRuleName + " " + body;
    } else {
      pending.text = body;

      std::size_t k = 0;
      std::string name;
      while (k < body.size() && AbnfChars::isRuleChar(body[k])) name += body[k++];  //< "params = *14(x)" -> "params"
      if (!name.empty()) lastRuleName = name;  //< remembered so a later bare "=/" can find its owner
    }
    pending.number = physical;  //< the FIRST physical line of this rule, for errors
  }

  if (!pending.text.empty()) out.push_back(pending);  //< last rule in the file, never flushed by a successor
  return true;
}

}  // namespace Abnf
