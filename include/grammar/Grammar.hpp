/**
 * @file Grammar.hpp
 * @brief The compiled grammar: a flat, immutable arena of AST nodes.
 *
 * Stage 2's product. GrammarBuilder fills it, GrammarValidator checks it, and
 * both matchers read it without ever modifying it.
 *
 * The representation is six parallel vectors and nothing else:
 * @verbatim
 *   _nodes         all GrammarNode, in creation order; index 0 is not special
 *   _children      one flat run of child indices per parent, never reused
 *   _literals      the text of every quoted string ("JOIN", ":", ...)
 *   _captureNames  every distinct $name, in first-seen order
 *   _ruleNames     every rule name, lower-cased, in first-seen order
 *   _ruleRoots     _ruleRoots[r] = node index of rule r's body, or kNoRule
 * @endverbatim
 *
 * Nodes address each other by @c int index, never by pointer. That is the
 * whole design decision behind this class: the vectors stay free to reallocate
 * while the tree is being built, a Grammar copies with plain vector copies, and
 * a bad index is a bounds bug rather than a dangling pointer.
 *
 * @note All accessors are @c const. The only way to build one is GrammarBuilder,
 *       which is a @c friend; there is no public mutator besides clear().
 */
#ifndef GRAMMAR_HPP
#define GRAMMAR_HPP

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

#include "grammar/GrammarNode.hpp"

namespace Abnf {
/** @brief Owns the node arena and the name tables for one grammar. */
class Grammar {
 public:
  Grammar();
  Grammar(const Grammar& other);
  Grammar& operator=(const Grammar& other);
  ~Grammar();

  /**
   * @brief Looks a rule up by name.
   * @param name Rule name; folded to lower case before comparing, so "MESSAGE"
   *             and "message" find the same rule.
   * @return The rule index, or kNoRule if there is no such rule.
   * @note Linear scan. Called a handful of times at startup (Server binds each
   *       command's rule once), never per message, so it is not worth a map.
   */
  int ruleIndex(const std::string& name) const;

  /** @brief Sentinel for "no such rule" / "rule not yet defined" (value -1). */
  static const int kNoRule;

  /**
   * @brief @return Index of @p rule's root node, or kNoRule if @p rule is out
   *         of range or was referenced but never defined.
   * @note A root of kNoRule after a build is exactly what
   *       GrammarValidator::checkAllRulesDefined() rejects.
   */
  int ruleRoot(int rule) const;

  /** @brief @return @p rule's lower-cased name, or "" if out of range. */
  const std::string& ruleName(int rule) const;

  /** @brief @return How many distinct rule names the grammar interned. */
  std::size_t ruleCount() const;

  /**
   * @brief @return The node at @p index.
   * @warning Unchecked -- @p index must come from ruleRoot(), child(), or a
   *          node's own @c first field. Those are the only valid producers.
   */
  const GrammarNode& node(int index) const;

  /**
   * @brief @return The child-node index stored at @p index in the child arena.
   * @note Read as @c child(parent.first + k) for k in [0, parent.count).
   * @warning Unchecked, for the same reason as node().
   */
  int child(int index) const;

  /** @brief @return The literal text at @p index. @warning Unchecked. */
  const std::string& literal(int index) const;

  /** @brief @return The capture name at @p index, or "" if out of range. */
  const std::string& captureName(int index) const;

  /** @brief @return How many distinct $captures the grammar declares. */
  std::size_t captureCount() const;

  /**
   * @brief Looks a capture up by name.
   * @param name Capture name WITHOUT the '$'; compared case-sensitively,
   *             unlike rule names.
   * @return The capture index, or -1.
   * @note This index is what MatchResult uses as a slot number, and what
   *       Server::fillParams() compares against to skip the command and
   *       prefix captures when assembling Message::params.
   */
  int captureIndex(const std::string& name) const;

  /** @brief @return true when no rule has been interned yet. */
  bool isEmpty() const;

  /** @brief Drops every vector, returning the object to its default state. */
  void clear();

 private:
  //< GrammarBuilder writes these vectors directly. Granting it friendship is
  //< what lets every accessor above stay const with no mutating twin.
  friend class GrammarBuilder;

  std::vector<GrammarNode> _nodes;         //< The node arena; indices are ids.
  std::vector<int> _children;              //< Flat child runs; see file doc.
  std::vector<std::string> _literals;      //< Quoted-string texts.
  std::vector<std::string> _captureNames;  //< $names, case preserved.
  std::vector<std::string> _ruleNames;     //< Rule names, lower-cased.
  std::vector<int> _ruleRoots;             //< Parallel to _ruleNames.
};

/** @brief Debug dump: rule and capture counts, not the whole tree. */
std::ostream& operator<<(std::ostream& os, const Grammar& grammar);

}  // namespace Abnf

#endif
