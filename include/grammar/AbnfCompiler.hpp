#ifndef ABNFCOMPILER_HPP
#define ABNFCOMPILER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "grammar/Grammar.hpp"

class AbnfCompiler {
 public:
  AbnfCompiler();

  bool compile(const std::string& text, Grammar& out);
  const std::string& error() const;

 private:
  AbnfCompiler(const AbnfCompiler& other);
  AbnfCompiler& operator=(const AbnfCompiler& other);

  int internRule(const std::string& name);
  int internCapture(const std::string& name);
  int addNode(const GrammarNode& node);
  int addChildren(const std::vector<int>& children);

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

#endif
