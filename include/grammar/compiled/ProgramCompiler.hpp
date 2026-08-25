/**
 * @file ProgramCompiler.hpp
 * @brief Lowers one Grammar rule into a Program.
 *
 * The code generator of the compiled strategy:
 * @verbatim
 *   Grammar rule --ProgramCompiler--> Program --ProgramMatcher--> MatchResult
 *                  ^^^^^^^^^^^^^^^
 * @endverbatim
 *
 * @section pc_how How the lowering works
 *
 * A single recursive walk, emitNode(), one case per GrammarNode::Kind:
 * @verbatim
 *   OctetRange   -> one Class op over the range's bits
 *   Literal      -> one Class op per character, each holding both cases
 *   Sequence     -> its children, back to back; no instruction of its own
 *   Alternation  -> Split before each branch, Jump past the rest after it
 *   Repetition   -> see emitRepetition()
 *   Reference    -> INLINED: the referenced rule's body is emitted right here
 *   $capture     -> Save 2n ... body ... Save 2n+1
 * @endverbatim
 *
 * Two optimisations are worth knowing about, because they are why the compiled
 * programs stay small:
 *
 *  - @b Class @b folding. isSingleOctet() asks "can this whole subtree only
 *    ever match exactly one octet?" If yes, buildClass() flattens it into one
 *    bitmap and the subtree becomes a single Class op. @c nospcrlfcl -- five
 *    alternated ranges -- compiles to one instruction.
 *  - @b Class @b dedup. addClass() memcmp's each new bitmap against the ones
 *    already stored and reuses a match, so the dozens of rules that bottom out
 *    in @c middle all share one table.
 *
 * @section pc_limits What it refuses, and why
 *
 * Inlining is what makes the VM flat and fast, and it is also the source of
 * both limitations. Neither is a bug; both are reported as errors at startup:
 *
 *  1. @b Recursion. Inlining a rule that references itself would not terminate,
 *     so a reference to a rule already being emitted is rejected. TreeMatcher
 *     has no such limit -- that is a real reason to prefer it as the default.
 *  2. @b Captures @b under @b an @b unbounded @b repetition. A loop reuses one
 *     slot pair, so @c *( $x ) would only ever report its last match. A BOUNDED
 *     repetition is fine: it is unrolled, so each iteration gets its own slots.
 *     That is why the embedded grammar writes @c *13( SPACE $modeparam ) with
 *     an explicit bound rather than a bare @c *.
 */
#ifndef PROGRAMCOMPILER_HPP
#define PROGRAMCOMPILER_HPP

#include <string>
#include <vector>

#include "grammar/Grammar.hpp"
#include "grammar/compiled/Program.hpp"

namespace Abnf {
namespace Compiled {
/** @brief One-rule-at-a-time code generator; reusable across calls. */
class ProgramCompiler {
 public:
  ProgramCompiler();
  ~ProgramCompiler();

  /**
   * @brief Compiles rule @p rule of @p grammar into @p out.
   * @param grammar  Grammar to read; borrowed for the call only.
   * @param rule     Rule index, from Grammar::ruleIndex().
   * @param[out] out Receives the program; clear()ed first, and cleared AGAIN
   *                 on failure so a caller never sees half a program.
   * @return true on success; false with error() set.
   * @note Always appends a trailing Instruction::Match, so a program that runs
   *       off its own end is impossible.
   */
  bool compile(const Grammar& grammar, int rule, Program& out);

  /** @brief @return Why compilation failed, prefixed "program: ", or "". */
  const std::string& error() const;

 private:
  //< Non-copyable: _grammar and _program are borrowed pointers, live only for
  //< the duration of compile(). Declared and never defined.
  ProgramCompiler(const ProgramCompiler& other);
  ProgramCompiler& operator=(const ProgramCompiler& other);

  /**
   * @brief Appends one instruction.
   * @return Its address, so a forward branch can be patched once its target
   *         is known -- the standard emit-then-backpatch dance used by every
   *         Split and Jump in here.
   */
  int emit(Instruction::Op op, int x, int y);

  /**
   * @brief Emits code for @p node. The recursive heart of the compiler.
   * @return true on success; false with error() set.
   * @note Tries the single-octet class folding first, then dispatches on kind.
   */
  bool emitNode(int node);

  /**
   * @brief Emits a Repetition node.
   * @return true on success; false with error() set.
   * @note Two shapes. UNBOUNDED becomes a real loop -- Split, body, Jump back
   *       -- which is compact but reuses its slots, hence the capture refusal.
   *       BOUNDED is unrolled: the mandatory `lo` copies are emitted straight,
   *       then `hi - lo` optional copies each guarded by its own Split. Unrolling
   *       is capped, so a huge bound is an error rather than a huge program.
   */
  bool emitRepetition(int node);

  /** @brief Emits @p node exactly @p times in a row; the unroll primitive. */
  bool emitBody(int node, int times);

  /**
   * @brief Interns a 32-byte class bitmap.
   * @param bits The 256-bit table.
   * @return Its class index, reusing an identical existing table if there is
   *         one. Linear memcmp scan -- fine, since it runs at startup only.
   */
  int addClass(const unsigned char* bits);

  /**
   * @brief Flattens @p node's octets into @p bits.
   * @param node       Subtree to flatten; must already have passed isSingleOctet().
   * @param[in,out] bits 32-byte table, pre-zeroed by the caller; bits are OR'd in.
   * @return false if @p node turns out not to be foldable after all.
   * @note A one-character Literal sets BOTH cases, matching RFC 5234's rule
   *       that a quoted string is case-insensitive.
   */
  bool buildClass(int node, unsigned char* bits) const;

  /**
   * @brief Can @p node only ever match exactly one octet?
   * @return true if the whole subtree collapses to a character class.
   * @note Memoised in _octetMemo. The memo doubles as a cycle guard: entries
   *       are marked "in progress" on the way in, so a recursive rule answers
   *       false instead of looping.
   * @note A captured reference answers false even when its body is one octet
   *       -- folding it away would lose the Save pair.
   */
  bool isSingleOctet(int node) const;

  /**
   * @brief Does @p node contain a capture anywhere beneath it?
   * @note Used by emitRepetition() to decide whether an unbounded loop must be
   *       refused. Follows Reference nodes into the rules they name.
   */
  bool hasCapture(int node) const;

  /** @brief Records a failure as "program: @p message". @return Always false. */
  bool fail(const std::string& message);

  const Grammar* _grammar;       //< Borrowed; valid only during compile().
  Program* _program;             //< Borrowed output; likewise.
  std::vector<char> _compiling;  //< Per-rule "being inlined" flag; catches recursion.
  std::vector<char> _octetMemo;  //< Per-node isSingleOctet() memo: 0 unknown, 1 yes, 2 no.
  std::string _error;            //< Last failure; "" means success.
};

}  // namespace Compiled
}  // namespace Abnf

#endif
