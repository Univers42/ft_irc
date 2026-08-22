#ifndef ABNFCOMPILER_HPP
#define ABNFCOMPILER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "grammar/Grammar.hpp"

/* Compiles ABNF text (RFC 5234) into a Grammar.
**
** Supports the subset RFC 2812 actually uses:
**
**   rule = elements        rule definition
**   rule =/ elements       incremental alternative
**   a / b                  alternation
**   a b                    concatenation
**   *x  1*x  *14(x)  5(x)  repetition
**   [ x ]  ( x )           option, grouping
**   "PASS"                 literal, case-INsensitive per RFC 5234, which is
**                          exactly IRC command semantics
**   %x20  %x21-39  %x0D.0A octet value, range, dotted concatenation
**   ; text                 comment
**   <indented line>        continuation
**
** One extension: `$name` marks a rule reference as a CAPTURE, recording the
** span it matched. Standard ABNF has no way to say "and remember this part".
**
** compile() refuses anything it cannot honestly build -- an undefined rule, a
** duplicate definition, a left-recursive rule -- and error() says which and on
** what line. Callers treat that as fatal at startup: a grammar that half
** compiled would fail later and far less legibly.
**
** The work is split three ways. AbnfLineReader handles the lexical layer
** (comments, folding, the RFC's elided '=/'), this class parses the resulting
** logical lines into nodes, and GrammarValidator checks the finished graph.
*/
class AbnfCompiler {
 public:
  AbnfCompiler();

  bool compile(const std::string& text, Grammar& out);
  const std::string& error() const;

 private:
  AbnfCompiler(const AbnfCompiler& other);
  AbnfCompiler& operator=(const AbnfCompiler& other);

  /* ─── interning and construction ─── */
  int internRule(const std::string& name);
  int internCapture(const std::string& name);
  int addNode(const GrammarNode& node);
  int addChildren(const std::vector<int>& children);

  /* ─── recursive descent over one logical line ─── */
  bool parseRule(const std::string& line, std::size_t lineNo);
  bool parseAlternation(const std::string& s, std::size_t& i, int& out);
  bool parseConcatenation(const std::string& s, std::size_t& i, int& out);
  bool parseRepetition(const std::string& s, std::size_t& i, int& out);
  bool parseElement(const std::string& s, std::size_t& i, int& out);
  bool parseNumericValue(const std::string& s, std::size_t& i, int& out);

  bool fail(const std::string& message);

  Grammar* _grammar;
  std::string _error;
  std::size_t _lineNo;
};

#endif /* ABNFCOMPILER_HPP */
