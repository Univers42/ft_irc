#ifndef TREEMATCHER_HPP
#define TREEMATCHER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "grammar/Grammar.hpp"
#include "grammar/IMatcher.hpp"
#include "grammar/MatchResult.hpp"

namespace Abnf {
namespace Interpreted {
class TreeMatcher : public IMatcher {
 public:
  static const long kMaxSteps;
  static const int kMaxDepth;

  explicit TreeMatcher(const Grammar& grammar);

  virtual bool match(int rule, const std::string& line,
                     MatchResult& out) const;

  virtual const char* strategy() const;

  virtual bool lastExhausted() const;

  const Grammar& grammar() const;

 private:
  TreeMatcher(const TreeMatcher& other);
  TreeMatcher& operator=(const TreeMatcher& other);

  enum ContinuationKind {
    ContNode,
    ContSequence,
    ContRepeat,
    ContCloseCapture
  };

  struct Continuation {
    ContinuationKind kind;
    int node;
    int counter;
    std::size_t start;
    const Continuation* next;
  };

  struct Walk {
    const std::string* line;
    std::vector<std::string> values;
    std::vector<char> present;
    long steps;
    int depth;
    bool exhausted;
  };

  bool matchNode(int node, std::size_t pos, const Continuation* next,
                 Walk& walk) const;
  bool matchContinuation(const Continuation* k, std::size_t pos,
                         Walk& walk) const;
  bool matchSequence(int node, int childNo, std::size_t pos,
                     const Continuation* next, Walk& walk) const;
  bool matchRepetition(int node, int count, std::size_t iterStart,
                       std::size_t pos, const Continuation* next,
                       Walk& walk) const;

  bool isSingleOctet(int node) const;
  bool octetMatches(int node, unsigned char c) const;
  const unsigned char* octetBitmap(int node) const;

  const Grammar& _grammar;
  mutable bool _exhausted;

  mutable std::vector<char> _singleOctet;

  mutable std::vector<unsigned char> _bitmaps;
  mutable std::vector<char> _bitmapBuilt;
};

}  // namespace Interpreted
}  // namespace Abnf

#endif
