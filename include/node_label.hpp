#pragma once

#include <cstddef>

class CompactGenome;
class LeafSet;

/*
 * NodeLabel stores the data which formally defines a node: a CompactGenome,
 * and a set of child clades, stored in a LeafSet. This allows appropriate
 * equality testing between node objects during MAD operations, like merging.
 */
class NodeLabel {
 public:
  NodeLabel();
  NodeLabel(const CompactGenome* cg, const LeafSet* ls);

  const CompactGenome* GetCompactGenome() const;
  const LeafSet* GetLeafSet() const;

  void SetCompactGenome(const CompactGenome* cg);
  void SetLeafSet(const LeafSet* ls);

  bool operator==(const NodeLabel& rhs) const noexcept;

  [[nodiscard]] size_t Hash() const noexcept;

 private:
  const CompactGenome* compact_genome_;
  const LeafSet* leaf_set_;
};

namespace std {
template <typename>
struct hash;
template <typename>
struct equal_to;
}  // namespace std

template <>
struct std::hash<NodeLabel> {
  std::size_t operator()(const NodeLabel& nl) const noexcept { return nl.Hash(); }
};

template <>
struct std::equal_to<NodeLabel> {
  std::size_t operator()(const NodeLabel& lhs, const NodeLabel& rhs) const noexcept {
    return lhs == rhs;
  }
};