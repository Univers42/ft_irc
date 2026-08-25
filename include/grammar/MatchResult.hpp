/**
 * @file MatchResult.hpp
 * @brief What a successful match hands back: the captured spans.
 *
 * Stage 4, the end of the pipeline. Filled by whichever IMatcher ran, read by
 * Server::parseLine() and by every command handler downstream.
 *
 * @section mr_two Two views of the same captures
 *
 * The same data is exposed twice, because two callers want it two ways:
 *
 *  - @b By @b name -- get("chanlist"), count(), at(). One slot per distinct
 *    $name in the grammar, each holding every value that name captured. This
 *    is what handlers use.
 *  - @b In @b order -- sequenceSize(), sequenceAt(), sequenceOwner(). Every
 *    capture flattened into one list, in the order the line produced them.
 *    Server::fillParams() needs this: IRC parameters are positional, so
 *    "MODE #c +ov a b" must keep a and b in order even though both landed in
 *    the same $modeparam slot.
 *
 * @section mr_own Ownership
 *
 * MatchResult holds a borrowed Grammar pointer for name lookups and owns its
 * strings. The adopt* methods take their argument by non-const reference and
 * @c swap it in -- the matcher builds the vectors in its own scratch space and
 * hands them over without a copy. Nobody outside a matcher should call them.
 */
#ifndef MATCHRESULT_HPP
#define MATCHRESULT_HPP

#include <cstddef>
#include <iosfwd>
#include <string>
#include <vector>

namespace Abnf {
class Grammar;

/** @brief The capture set produced by one successful IMatcher::match(). */
class MatchResult {
 public:
  /** @brief Empty and unbound; every lookup fails until reset() is called. */
  MatchResult();
  MatchResult(const MatchResult& other);
  MatchResult& operator=(const MatchResult& other);
  ~MatchResult();

  /** @brief @return true if capture @p name grabbed at least one value. */
  bool has(const std::string& name) const;

  /**
   * @brief @return The first value captured under @p name.
   * @note Returns a reference to a shared empty string when the name is
   *       unknown or captured nothing, so the result is always safe to read.
   *       Use has() when "absent" and "empty" need telling apart.
   */
  const std::string& get(const std::string& name) const;

  /** @brief @return How many values @p name captured; 0 if unknown. */
  std::size_t count(const std::string& name) const;

  /**
   * @brief @return The @p index -th value captured under @p name.
   * @note Out-of-range reads give "" rather than undefined behaviour, so a
   *       caller may probe without checking count() first.
   */
  const std::string& at(const std::string& name, std::size_t index) const;

  /* A capture read as a separated list -- "#a,#b" as two fields rather than
  ** one string. Nothing about splitting a capture is protocol-specific, so it
  ** belongs beside at() rather than in whatever type happens to hold the
  ** result; Message had both of these and did nothing with them but forward.
  ** list() drops empty fields, which is what a comma-separated protocol list
  ** ("#a,,#b,") almost always means; listKeepEmpty() keeps them, for the
  ** parallel lists where position carries the meaning. */

  /**
   * @brief Splits the first value of @p name on @p separator, dropping empties.
   * @note For "#a,,#b," this yields {"#a", "#b"} -- what a channel list means.
   */
  std::vector<std::string> list(const std::string& name, char separator) const;

  /**
   * @brief Splits the first value of @p name on @p separator, keeping empties.
   * @note For the parallel channel/key lists of JOIN, where the n-th key
   *       belongs to the n-th channel and an empty field still holds a place.
   */
  std::vector<std::string> listKeepEmpty(const std::string& name, char separator) const;

  /** @brief Drops every value AND the grammar binding, back to default state. */
  void clear();

  /**
   * @brief Rebinds to @p grammar and clears the values.
   * @param grammar Grammar whose capture names this result will resolve
   *                against; borrowed, so it must outlive the result.
   * @note Called at the top of every IMatcher::match(), which is why a caller
   *       may hand the same MatchResult to call after call.
   */
  void reset(const Grammar& grammar);

  /**
   * @brief Takes ownership of the by-name values by swapping them in.
   * @param[in,out] values One vector per capture slot; left empty on return.
   * @warning Matcher-internal. @p values must already be sized to
   *          Grammar::captureCount() -- reset() sizes the member that way.
   */
  void adopt(std::vector<std::vector<std::string> >& values);

  /**
   * @brief Takes ownership of the ordered view by swapping it in.
   * @param[in,out] sequence Captured texts in line order; emptied on return.
   * @param[in,out] owners   Parallel capture indices; emptied on return.
   * @warning Matcher-internal, and the two vectors must be the same length.
   */
  void adoptSequence(std::vector<std::string>& sequence, std::vector<int>& owners);

  /** @brief @return Total number of captured values, across all names. */
  std::size_t sequenceSize() const;

  /** @brief @return The @p index -th capture in line order; "" if out of range. */
  const std::string& sequenceAt(std::size_t index) const;

  /**
   * @brief @return Which capture slot produced sequenceAt(@p index), or -1.
   * @note Compare against Grammar::captureIndex("command") and friends. That
   *       is exactly how Server::fillParams() drops the command and prefix
   *       captures while keeping everything else as positional parameters.
   */
  int sequenceOwner(std::size_t index) const;

 private:
  friend std::ostream& operator<<(std::ostream& os, const MatchResult& result);

  /**
   * @brief @return The slot index for capture @p name, or -1 if unknown.
   * @note Linear scan over the grammar's capture names, case-sensitive. Every
   *       public by-name accessor funnels through here.
   */
  int slotOf(const std::string& name) const;

  const Grammar* _grammar;                         //< Borrowed; NULL until reset().
  std::vector<std::vector<std::string> > _values;  //< By slot; see mr_two.
  std::vector<std::string> _sequence;              //< Same values, in line order.
  std::vector<int> _owners;                        //< Slot of each _sequence entry.
};

/** @brief Debug dump as "{name=value, name=value}", or "{unbound}". */
std::ostream& operator<<(std::ostream& os, const MatchResult& result);

}  // namespace Abnf

#endif
