/*
   This file copyright (c) 2011-2013, the Quickstep authors.
   See file CREDITS.txt for details.
  
   This file is part of Quickstep.

   Quickstep is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
  
   Quickstep is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with Quickstep.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef QUICKSTEP_STORAGE_COLUMN_STORE_UTIL_HPP_
#define QUICKSTEP_STORAGE_COLUMN_STORE_UTIL_HPP_

#include <cstddef>
#include <iterator>

#include "catalog/CatalogTypedefs.hpp"
#include "storage/StorageBlockInfo.hpp"
#include "utility/Macros.hpp"

namespace quickstep {

class BasicColumnStoreTupleStorageSubBlock;
class CatalogRelation;
class CompressedColumnStoreTupleStorageSubBlock;
class Predicate;
class TupleIdSequence;
class UncheckedComparator;

namespace column_store_util {

/** \addtogroup Storage
 *  @{
 */

/**
 * @brief An iterator over the values in a column stripe. Used internally by
 *        BasicColumnStoreTupleStorageSubBlock and
 *        CompressedColumnStoreTupleStorageSubBlock.
 **/
class ColumnStripeIterator : public std::iterator<std::random_access_iterator_tag, void*> {
 public:
  typedef std::iterator<std::random_access_iterator_tag, void*>::difference_type difference_type;

  ColumnStripeIterator()
      : stripe_start_(NULL),
        attr_length_(0),
        tuple_position_(0) {
  }

  ColumnStripeIterator(const ColumnStripeIterator &other)
      : stripe_start_(other.stripe_start_),
        attr_length_(other.attr_length_),
        tuple_position_(other.tuple_position_) {
  }

  ColumnStripeIterator& operator=(const ColumnStripeIterator& other) {
    if (this != &other) {
      stripe_start_ = other.stripe_start_;
      attr_length_ = other.attr_length_;
      tuple_position_ = other.tuple_position_;
    }
    return *this;
  }

  // Comparisons.
  inline bool operator==(const ColumnStripeIterator& other) const {
    DEBUG_ASSERT(stripe_start_ == other.stripe_start_);
    DEBUG_ASSERT(attr_length_ == other.attr_length_);
    return tuple_position_ == other.tuple_position_;
  }

  inline bool operator!=(const ColumnStripeIterator& other) const {
    DEBUG_ASSERT(stripe_start_ == other.stripe_start_);
    DEBUG_ASSERT(attr_length_ == other.attr_length_);
    return tuple_position_ != other.tuple_position_;
  }

  inline bool operator<(const ColumnStripeIterator& other) const {
    DEBUG_ASSERT(stripe_start_ == other.stripe_start_);
    DEBUG_ASSERT(attr_length_ == other.attr_length_);
    return tuple_position_ < other.tuple_position_;
  }

  inline bool operator<=(const ColumnStripeIterator& other) const {
    DEBUG_ASSERT(stripe_start_ == other.stripe_start_);
    DEBUG_ASSERT(attr_length_ == other.attr_length_);
    return tuple_position_ <= other.tuple_position_;
  }

  inline bool operator>(const ColumnStripeIterator& other) const {
    DEBUG_ASSERT(stripe_start_ == other.stripe_start_);
    DEBUG_ASSERT(attr_length_ == other.attr_length_);
    return tuple_position_ > other.tuple_position_;
  }

  inline bool operator>=(const ColumnStripeIterator& other) const {
    DEBUG_ASSERT(stripe_start_ == other.stripe_start_);
    DEBUG_ASSERT(attr_length_ == other.attr_length_);
    return tuple_position_ >= other.tuple_position_;
  }

  // Increment/decrement.
  inline ColumnStripeIterator& operator++() {
    ++tuple_position_;
    return *this;
  }

  ColumnStripeIterator operator++(int) {  // NOLINT - increment operator doesn't need named param
    ColumnStripeIterator result(*this);
    ++(*this);
    return result;
  }

  inline ColumnStripeIterator& operator--() {
    --tuple_position_;
    return *this;
  }

  ColumnStripeIterator operator--(int) {  // NOLINT - decrement operator doesn't need named param
    ColumnStripeIterator result(*this);
    --(*this);
    return result;
  }

  // Compound assignment.
  inline ColumnStripeIterator& operator+=(difference_type n) {
    tuple_position_ += n;
    return *this;
  }

  inline ColumnStripeIterator& operator-=(difference_type n) {
    tuple_position_ -= n;
    return *this;
  }

  // Note: + operator with difference_type on the left is not defined.
  ColumnStripeIterator operator+(difference_type n) const {
    return ColumnStripeIterator(stripe_start_, attr_length_, tuple_position_ + n);
  }

  ColumnStripeIterator operator-(difference_type n) const {
    return ColumnStripeIterator(stripe_start_, attr_length_, tuple_position_ - n);
  }

  difference_type operator-(const ColumnStripeIterator &other) const {
    DEBUG_ASSERT(stripe_start_ == other.stripe_start_);
    DEBUG_ASSERT(attr_length_ == other.attr_length_);
    return tuple_position_ - other.tuple_position_;
  }

  // Dereference.
  inline void* operator*() const {
    return static_cast<char*>(stripe_start_) + tuple_position_ * attr_length_;
  }

  inline void** operator->() const {
    FATAL_ERROR("-> dereference operator unimplemented for ColumnStripeIterator.");
  }

  void* operator[](difference_type n) const {
    return static_cast<char*>(stripe_start_) + (tuple_position_ + n) * attr_length_;
  }

  tuple_id getTuplePosition() const {
    return tuple_position_;
  }

 private:
  ColumnStripeIterator(void *stripe_start,
                       const std::size_t attr_length,
                       const tuple_id tuple_position)
      : stripe_start_(stripe_start),
        attr_length_(attr_length),
        tuple_position_(tuple_position) {
  }

  void *stripe_start_;
  std::size_t attr_length_;
  tuple_id tuple_position_;

  friend class quickstep::BasicColumnStoreTupleStorageSubBlock;
  friend class SortColumnPredicateEvaluator;
};

/**
 * @brief A class which contains static helper methods for predicate evaluation
 *        on the sort column of a BasicColumnStoreTupleStorageSubBlock or a
 *        CompressedColumnStoreTupleStorageSubBlock.
 **/
class SortColumnPredicateEvaluator {
 public:
  /**
   * @brief Attempt to evaluate a predicate on an uncompressed sorted column of
   *        a BasicColumnStoreTupleStorageSubBlock or
   *        CompressedColumnStoreTupleStorageSubBlock.
   * @note Currently this method can only evaluate a simple comparison of a
   *       literal value with the value of a sort attribute. It will return
   *       NULL if called with a predicate it does not know how to evaluate.
   *
   * @param predicate A predicate to attempt to evaluate.
   * @param relation The relation which the TupleStorageSubBlock which uses
   *        this method belongs to.
   * @param sort_attribute_id The ID of the sort column attribute in relation.
   * @param sort_column_stripe A sorted, packed, and uncompressed column stripe
   *        of values belonging to the attribute with sort_attribute_id in
   *        relation.
   * @param num_tuples The number of tuples in the TupleStorageSubBlock which
   *        uses this method (i.e. the number of values in sort_column_stripe).
   * @return A sequence of tuple_ids which match predicate, or NULL if this
   *         method is unable to evaluate predicate (in which case the default
   *         TupleStorageSubBlock::getMatchesForPredicate() method should be
   *         used as a fallback).
   **/
  static TupleIdSequence* EvaluatePredicateForUncompressedSortColumn(
      const Predicate &predicate,
      const CatalogRelation &relation,
      const attribute_id sort_attribute_id,
      void *sort_attribute_stripe,
      const tuple_id num_tuples);

 private:
  // Undefined default constructor - class is all static and should not be
  // instantiated.
  SortColumnPredicateEvaluator();

  DISALLOW_COPY_AND_ASSIGN(SortColumnPredicateEvaluator);
};

/** @} */

}  // namespace column_store_util
}  // namespace quickstep

#endif  // QUICKSTEP_STORAGE_COLUMN_STORE_UTIL_HPP_
