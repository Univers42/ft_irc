#ifndef GRAMMARVALIDATOR_HPP
#define GRAMMARVALIDATOR_HPP

#include <string>
#include <vector>

#include "grammar/Grammar.hpp"

namespace Abnf {
class GrammarValidator {
 public:
  GrammarValidator();
  ~GrammarValidator();

  bool validate(const Grammar& grammar);

  const std::string& error() const;

 private:
  GrammarValidator(const GrammarValidator& other);
  GrammarValidator& operator=(const GrammarValidator& other);

  bool checkAllRulesDefined();
  bool checkNoLeftRecursion();

  bool isNullable(int node, std::vector<char>& busy) const;

  void collectLeftReachable(int node, std::vector<char>& seen, std::vector<char>& busy) const;

  const Grammar* _grammar;
  std::string _error;
};

}  // namespace Abnf

#endif
