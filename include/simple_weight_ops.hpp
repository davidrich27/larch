/**
  A way to build a WeightOp type possessing all the values required by SubtreeWeight,
using simpler-to-define binary operations.

  The template parameter BinaryOperatorWeightOps is expected to provide at least the
values in the following example struct:

struct ExampleBinaryOperatorWeightOps {
  using Weight = size_t;  // Provide any weight type you like
  constexpr static Weight Identity = 0;  // The identity element under Combine
  inline Weight ComputeLeaf(const MADAG& dag, NodeId node_id);  // The value assigned to
a leaf node inline Weight ComputeEdge(const MADAG& dag, EdgeId edge_id);  // The value
assigned to an edge inline bool Compare(Weight lhs, Weight rhs);  // The ordering
operator: is lhs 'better than' rhs inline bool CompareEqual(Weight lhs, Weight rhs);  //
A custom implemented equality inline Weight Combine(Weight lhs, Weight rhs);  // A
binary operation that respects ordering, e.g. '+'
};

 */

#pragma once

#include "mutation_annotated_dag.hpp"

template <typename BinaryOperatorWeightOps>
struct SimpleWeightOps {
  using Weight = typename BinaryOperatorWeightOps::Weight;
  BinaryOperatorWeightOps&& binary_operator_weight_ops_ = BinaryOperatorWeightOps();
  inline Weight ComputeLeaf(const MADAG& dag, NodeId node_id);
  inline Weight ComputeEdge(const MADAG& dag, EdgeId edge_id);
  inline std::pair<Weight, std::vector<size_t>> WithinCladeAccumOptimum(
      std::vector<Weight>);
  inline Weight BetweenClades(std::vector<Weight>);
  inline Weight AboveNode(Weight eddgeweight, Weight childnodeweight);
};

#include "impl/simple_weight_ops_impl.hpp"