/**
 * @file GrammarBuilder.hpp
 * @brief Recursive-descent parser: ABNF text in, Grammar out.
 *
 * Stage 2, and the real piece of work in this module:
 * @verbatim
 *   IGrammarSource -> AbnfLineReader -> GrammarBuilder -> Grammar -> IMatcher
 *                                       ^^^^^^^^^^^^^^
 * @endverbatim
 *
 * @section gb_grammar The grammar of the grammar
 *
 * The private parse* methods are one method per precedence level, lowest
 * binding first. Read top to bottom and you have the whole language:
 *
 * @verbatim
 *   rule          = rulename ("=" / "=/") alternation
 *   alternation   = concatenation *( "/" concatenation )   <- loosest
 *   concatenation = 1*repetition
 *   repetition    = [ repeat ] element
 *   repeat        = 1*DIGIT / [1*DIGIT] "*" [1*DIGIT]
 *   element       = "(" alternation ")"      group
 *                 / "[" alternation "]"      option, becomes a 0..1 Repetition
 *                 / DQUOTE *char DQUOTE      literal, case-insensitive
 *                 / "%x" hex [ "-" hex ]     octet range, may be dotted
 *                 / [ "$" ] rulename         reference, '$' marks a capture
 * @endverbatim
 *
 * Each level calls the next one down and loops on its own operator; a group
 * recurses back to parseAlternation(). That is the entire parser.
 *
 * @section gb_ext Deviations from RFC 5234
 *
 *  - @b Added: a '$' prefix on a rule reference marks it as a capture, so the
 *    span it matched is recorded in MatchResult. Not RFC syntax; it is what
 *    lets the grammar double as the parameter extractor.
 *  - @b Missing: only the @c %x form of num-val is accepted -- no @c %d, no
 *    @c %b, no @c prose-val. The embedded grammar never needs them.
 *
 * @section gb_style Shape of the code
 *
 * Every parse* method has the same signature and contract: take the rule body
 * and a cursor, consume exactly what belongs to that level, write the node
 * index to @p out, and return false with error() set on failure. Callers may
 * assume the cursor did not move backwards.
 *
 * No node is created for a construct that has only one part -- a single-branch
 * alternation, a one-part concatenation and an unrepeated element all return
 * their child directly, so the AST carries no redundant wrappers.
 */
#ifndef GRAMMARBUILDER_HPP
#define GRAMMARBUILDER_HPP

#include <cstddef>
#include <string>
#include <vector>

#include "grammar/Grammar.hpp"

namespace Abnf {
/**
 * @brief Parses ABNF source into a Grammar and validates the result.
 *
 * Reusable across calls but single-shot per call: compile() clears the target
 * and re-points @c _grammar at it, so one builder may compile several grammars
 * in sequence, never two at once.
 */
class GrammarBuilder {
 public:
  GrammarBuilder();
  ~GrammarBuilder();

  /**
   * @brief Parses @p text and fills @p out. The only public entry point.
   * @param text     Whole ABNF source, as handed over by an IGrammarSource.
   * @param[out] out Receives the grammar; clear()ed first, so any previous
   *                 content is discarded even if the compile then fails.
   * @return true on success; false with error() set.
   * @note Three phases, in order: AbnfLineReader unfolds the text, parseRule()
   *       runs over every logical line, then GrammarValidator has the last
   *       word. A grammar that parses but is left-recursive still fails here.
   */
  bool compile(const std::string& text, Grammar& out);

  /**
   * @brief @return Description of the last failure, or "" if none.
   * @note Already formatted as "grammar: line N: what went wrong", so a caller
   *       can hand it straight to the user. Validator errors carry line 0,
   *       because a left-recursion cycle belongs to no single line.
   */
  const std::string& error() const;

 private:
  //< Non-copyable, and this one is not just convention: _grammar is a raw
  //< borrowed pointer into the caller's Grammar. Copying a builder mid-compile
  //< would leave two objects writing into the same target. Cheaper to forbid
  //< than to reason about. Declared, never defined -- a copy is a link error.
  GrammarBuilder(const GrammarBuilder& other);
  GrammarBuilder& operator=(const GrammarBuilder& other);

  /**
   * @brief Finds a rule by name, or creates it if unknown.
   * @param name Rule name as written in the source; folded to lower case, so
   *             "SPACE" and "space" resolve to the same rule (RFC 5234).
   * @return Index into Grammar::_ruleNames, stable for the whole build.
   * @note On creation the rule's root is set to Grammar::kNoRule, which is
   *       what lets a rule be referenced before it is defined. GrammarValidator
   *       later rejects any rule still left at kNoRule.
   */
  int internRule(const std::string& name);

  /**
   * @brief Finds a capture by name, or creates it if unknown.
   * @param name Capture name, taken from the rule name after '$'. Compared
   *             case-sensitively, unlike rule names.
   * @return Index into Grammar::_captureNames, used as GrammarNode::capture.
   */
  int internCapture(const std::string& name);

  /**
   * @brief Appends a node to the AST arena.
   * @param node Fully populated node; copied by value into the arena.
   * @return Index into Grammar::_nodes. This index is how nodes reference each
   *         other: the AST stores integers, never pointers, so the vectors stay
   *         free to reallocate and a Grammar copies as plain vector copies.
   */
  int addNode(const GrammarNode& node);

  /**
   * @brief Appends a contiguous run of child indices.
   * @param children Child node indices, in source order.
   * @return Start of the run, to be stored in GrammarNode::first, with
   *         children.size() stored in GrammarNode::count.
   * @note Runs are never reused or freed; each parent owns its own slice.
   */
  int addChildren(const std::vector<int>& children);

  /**
   * @brief Parses one complete rule definition and installs it in the Grammar.
   * @param line   One logical rule, already unfolded by AbnfLineReader
   *               (continuations joined, comments stripped).
   * @param lineNo Physical source line, kept only for error messages.
   * @return true on success; false with error() set.
   * @note Handles both "name = body" and the elided "name =/ body", the latter
   *       wrapping the previous root and the new body in an Alternation.
   *       Rejects a second plain '=' for an already-defined rule, and a "=/"
   *       for a rule that was never defined. A blank line is a silent success.
   */
  bool parseRule(const std::string& line, std::size_t lineNo);

  /**
   * @brief Parses concatenations separated by '/'. Lowest precedence level.
   * @param s        Rule body.
   * @param[in,out] i Cursor, advanced past what was consumed.
   * @param[out] out Receives the node index.
   * @return true on success; false with error() set.
   * @note With a single branch no Alternation node is created -- the child is
   *       returned directly, so the AST carries no redundant wrappers.
   */
  bool parseAlternation(const std::string& s, std::size_t& i, int& out);

  /**
   * @brief Parses a run of adjacent repetitions into a Sequence.
   * @param s        Rule body.
   * @param[in,out] i Cursor, advanced past what was consumed.
   * @param[out] out Receives the node index.
   * @return true on success; false with error() set.
   * @note Stops at '/', ')' or ']' WITHOUT consuming them -- the caller owns
   *       those. An empty run is an error ("a = / b", "a = ( )").
   */
  bool parseConcatenation(const std::string& s, std::size_t& i, int& out);

  /**
   * @brief Parses an optional repeat prefix followed by one element.
   * @param s        Rule body.
   * @param[in,out] i Cursor, advanced past what was consumed.
   * @param[out] out Receives the node index.
   * @return true on success; false with error() set.
   * @note Accepts all four repeat spellings: "3x" (exactly 3), "*x" (0..inf),
   *       "1*x" (1..inf) and "1*23x". A missing prefix yields the element
   *       itself, with no Repetition node wrapped around it.
   */
  bool parseRepetition(const std::string& s, std::size_t& i, int& out);

  /**
   * @brief Parses one element: the tightest-binding level.
   * @param s        Rule body.
   * @param[in,out] i Cursor, advanced past what was consumed.
   * @param[out] out Receives the node index.
   * @return true on success; false with error() set.
   * @note Dispatches on the first character: '(' group, '[' option (desugared
   *       to a 0..1 Repetition), '"' literal, '%' num-val, '$' capture prefix,
   *       or a letter starting a rule name. Recurses into parseAlternation()
   *       for '(' and '[', which is where the grammar's nesting comes from.
   */
  bool parseElement(const std::string& s, std::size_t& i, int& out);

  /**
   * @brief Parses a num-val, with the leading '%' already consumed.
   * @param s        Rule body.
   * @param[in,out] i Cursor, advanced past what was consumed.
   * @param[out] out Receives the node index.
   * @return true on success; false with error() set.
   * @note Only the @c %x form is supported. Handles the plain octet ("%x20"),
   *       the range ("%x41-5A") and the dotted concatenation ("%x0D.0A", which
   *       becomes a Sequence of two OctetRange nodes). Rejects a backwards
   *       range, since "%x5A-41" can never match anything.
   */
  bool parseNumericValue(const std::string& s, std::size_t& i, int& out);

  /**
   * @brief Records a failure as "grammar: line N: @p message".
   * @return Always false, so callers can write @c return @c fail("..."); and
   *         set the error and the return value in one statement.
   */
  bool fail(const std::string& message);

  Grammar* _grammar;    //< Borrowed, not owned; valid only during compile().
  std::string _error;   //< Last failure, pre-formatted; "" means success.
  std::size_t _lineNo;  //< Line fail() blames; set once per parseRule().
};

}  // namespace Abnf

#endif
