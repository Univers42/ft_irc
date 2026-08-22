#ifndef GRAMMAR_HPP
#define GRAMMAR_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "grammar/GrammarNode.hpp"

/* A compiled grammar: immutable, queryable, and cheap to walk.
**
** Grammar holds; AbnfCompiler builds. Keeping those apart matters because the
** matcher runs on every inbound line and has no business seeing the parsing
** machinery -- and because a Grammar that cannot be mutated after compilation
** is one fewer thing to reason about on the hot path.
**
** AbnfCompiler is the only friend, so it is the only way to populate one.
*/
class Grammar {
 public:
  Grammar();

  /* Rule names compare case-insensitively, as RFC 5234 requires.
  ** Returns kNoRule when the name is unknown. */
  int ruleIndex(const std::string& name) const;
  static const int kNoRule;

  /* Root node of a rule, or kNoRule when the rule was never defined. */
  int ruleRoot(int rule) const;
  const std::string& ruleName(int rule) const;
  std::size_t ruleCount() const;

  const GrammarNode& node(int index) const;
  int child(int index) const;
  const std::string& literal(int index) const;

  const std::string& captureName(int index) const;
  std::size_t captureCount() const;

  bool isEmpty() const;
  void clear();

 private:
  friend class AbnfCompiler;

  std::vector<GrammarNode> _nodes;
  std::vector<int> _children;
  std::vector<std::string> _literals;
  std::vector<std::string> _captureNames;
  std::vector<std::string> _ruleNames; /* stored lowercased */
  std::vector<int> _ruleRoots;
};

#endif /* GRAMMAR_HPP */
