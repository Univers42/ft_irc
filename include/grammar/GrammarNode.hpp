/**
 * @file GrammarNode.hpp
 * @brief One node of the parsed grammar tree.
 *
 * The whole AST is built out of this single struct. It is a tagged union done
 * the C++98 way -- a @c kind tag plus a fixed set of int fields whose meaning
 * depends on that tag. There are no pointers and no owned resources, so a node
 * is trivially copyable and a whole Grammar is just six vectors.
 *
 * Field meaning per kind:
 * @verbatim
 *   kind         which of the six shapes this node is
 *   lo / hi      OctetRange:  inclusive byte range, %x41-5A -> lo=0x41 hi=0x5A
 *                Repetition:  min / max count, *14 -> lo=0 hi=14
 *                             hi == kUnbounded means "no maximum" (a bare *)
 *                Reference:   lo is the rule index; hi unused
 *   first/count  Sequence, Alternation: start index into Grammar::_children
 *                and how many. Repetition always has count == 1.
 *   literal      index into Grammar::_literals, e.g. the "JOIN" of a command
 *   capture      index into Grammar::_captureNames, or kNoCapture. Set only
 *                where the source wrote '$' before a rule name.
 * @endverbatim
 *
 * @warning @c first is NOT used by Reference -- the rule index lives in @c lo.
 */
#ifndef GRAMMARNODE_HPP
#define GRAMMARNODE_HPP

#include <iosfwd>

namespace Abnf {
/** @brief A single AST node; see the file comment for the field encoding. */
struct GrammarNode {
  /**
   * @brief What shape this node is; selects how the int fields are read.
   *
   * These six cover all of RFC 5234's operators after desugaring:
   * an option @c [x] becomes a Repetition with lo=0, hi=1, and a group
   * @c (x) leaves no node of its own -- it only affects nesting.
   */
  enum Kind {
    Reference,    //< A named rule: `lo` is its index in Grammar::_ruleNames.
    Literal,      //< A quoted string; matched case-insensitively (RFC 5234).
    OctetRange,   //< A %xNN or %xNN-MM byte range, inclusive on both ends.
    Sequence,     //< Children matched one after another, all must match.
    Alternation,  //< Children tried in source order, first match wins.
    Repetition    //< One child matched between `lo` and `hi` times.
  };

  /** @brief Sentinel for GrammarNode::hi meaning "no upper limit" (value -1). */
  static const int kUnbounded;

  /** @brief Default node: an empty Sequence with no capture. */
  GrammarNode();
  GrammarNode(const GrammarNode& other);
  GrammarNode& operator=(const GrammarNode& other);
  ~GrammarNode();

  Kind kind;    //< Tag; decides how every field below is interpreted.
  int lo;       //< Range low byte, repeat minimum, or rule index. See file doc.
  int hi;       //< Range high byte, or repeat maximum (may be kUnbounded).
  int first;    //< Index of this node's first child in Grammar::_children.
  int count;    //< How many children start at `first`.
  int literal;  //< Index into Grammar::_literals, or -1 when not a Literal.

  int capture;  //< Index into Grammar::_captureNames, or kNoCapture.

  /** @brief Sentinel for GrammarNode::capture meaning "not captured" (-1). */
  static const int kNoCapture;
};

/** @brief Debug dump of one node: kind name plus the fields that kind uses. */
std::ostream& operator<<(std::ostream& os, const GrammarNode& node);

}  // namespace Abnf

#endif
