#include "grammar/AbnfLineReader.hpp"

#include <sstream>
#include <string>
#include <vector>

#include "grammar/AbnfChars.hpp"

namespace Abnf {
AbnfLineReader::AbnfLineReader() : _errorLine(0) {}

const std::string& AbnfLineReader::error() const { return _error; }

std::size_t AbnfLineReader::errorLine() const { return _errorLine; }

std::string AbnfLineReader::stripComment(const std::string& raw) {
  std::string out(raw);
  bool inQuote = false;

  for (std::string::size_type i = 0; i < out.size(); ++i) {
    if (out[i] == '"') {
      inQuote = !inQuote;
    } else if (out[i] == ';' && !inQuote) {
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

  Line pending;
  pending.number = 0;
  std::string lastRuleName;

  while (std::getline(in, raw)) {
    ++physical;

    if (!raw.empty() && raw[raw.size() - 1] == '\r') raw.erase(raw.size() - 1);
    raw = stripComment(raw);

    const std::string body = AbnfChars::trimmed(raw);
    if (body.empty()) continue;

    const bool continuation = AbnfChars::isBlank(raw[0]);
    const bool redefines = (body[0] == '=');

    if (continuation && !redefines && !pending.text.empty()) {
      pending.text += " ";
      pending.text += body;
      continue;
    }

    if (!pending.text.empty()) {
      out.push_back(pending);
      pending.text.clear();
    }

    if (redefines) {
      if (lastRuleName.empty()) {
        _errorLine = physical;
        _error = "'=' continuation with no rule name above it";
        return false;
      }
      pending.text = lastRuleName + " " + body;
    } else {
      pending.text = body;

      std::size_t k = 0;
      std::string name;
      while (k < body.size() && AbnfChars::isRuleChar(body[k])) name += body[k++];
      if (!name.empty()) lastRuleName = name;
    }
    pending.number = physical;
  }

  if (!pending.text.empty()) out.push_back(pending);
  return true;
}

}  // namespace Abnf
