#ifndef GRAMMAR_HPP
#define GRAMMAR_HPP

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "grammar/GrammarNode.hpp"

namespace Abnf {
class Grammar {
 public:
  Grammar();
  Grammar(const Grammar& other);
  Grammar& operator=(const Grammar& other);
  ~Grammar();

  int ruleIndex(const std::string& name) const;
  static const int kNoRule;

  int ruleRoot(int rule) const;
  const std::string& ruleName(int rule) const;
  std::size_t ruleCount() const;

  const GrammarNode& node(int index) const;
  int child(int index) const;
  const std::string& literal(int index) const;

  const std::string& captureName(int index) const;
  std::size_t captureCount() const;
  int captureIndex(const std::string& name) const;

  bool isEmpty() const;
  void clear();

 private:
  friend class GrammarBuilder;

  std::vector<GrammarNode> _nodes;
  std::vector<int> _children;
  std::vector<std::string> _literals;
  std::vector<std::string> _captureNames;
  std::vector<std::string> _ruleNames;
  std::vector<int> _ruleRoots;
};

std::ostream& operator<<(std::ostream& os, const Grammar& grammar);

}  // namespace Abnf

#endif
