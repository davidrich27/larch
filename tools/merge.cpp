#include <cstdlib>
#include <iostream>
#include <vector>
#include <algorithm>

#include "larch/subtree/subtree_weight.hpp"
#include "larch/subtree/parsimony_score_binary.hpp"
#include "arguments.hpp"
#include "larch/merge/merge.hpp"
#include "larch/dag_loader.hpp"
#include "larch/benchmark.hpp"

[[noreturn]] static void Usage() {
  std::cout << "Usage:\n";
  std::cout << "merge [-r,--refseq file] [-d,--dag] -i,--input file1 file2 ... "
               "[-o,--output filename]\n";
  std::cout << "  -i,--input     List of input files\n";
  std::cout << "  -o,--output    Save the output to filename (default is merged.pb)\n";
  std::cout << "  -r,--refseq    Read reference sequence from Json file\n";
  std::cout << "  -d,--dag       Input files are DAGs\n";
  std::cout << "  -t,--trim      Trim output to best parsimony\n";
  std::cout << "  -s,--sample    Sample a single tree from DAG\n";

  std::exit(EXIT_SUCCESS);
}

[[noreturn]] static void Fail() {
  std::cerr << "Run with -h or --help to see usage.\n";

  std::exit(EXIT_FAILURE);
}

static int MergeTrees(const std::vector<std::string_view>& paths,
                      std::string_view refseq_json_path, std::string_view out_path,
                      bool dags, bool trim, bool sample_tree) {
  std::vector<MADAGStorage<>> trees;
  std::string reference_sequence = "";
  if (not refseq_json_path.empty()) {
    reference_sequence = std::string{LoadReferenceSequence(refseq_json_path)};
  }

  trees.reserve(paths.size());
  std::vector<std::pair<size_t, std::string_view>> paths_idx;
  for (size_t i = 0; i < paths.size(); ++i) {
    trees.push_back(MADAGStorage<>::EmptyDefault());
    paths_idx.emplace_back(i, paths.at(i));
  }
  std::cout << "Loading trees ";
  ParallelForEach(paths_idx, [&](auto path_idx) {
    std::cout << "." << std::flush;
    trees.at(path_idx.first) =
        dags ? LoadDAGFromProtobuf(path_idx.second)
             : LoadTreeFromProtobuf(path_idx.second, reference_sequence);
    trees.at(path_idx.first).View().RecomputeCompactGenomes();
  });
  std::cout << " done."
            << "\n";

  Benchmark merge_time;
  Merge merge(trees.front().View().GetReferenceSequence());
  std::vector<MADAG> tree_refs{trees.begin(), trees.end()};
  merge_time.start();
  merge.AddDAGs(tree_refs);
  merge_time.stop();
  std::cout << "\nDAGs merged in " << merge_time.durationMs() << " ms\n";

  std::cout << "DAG leave(without trimming): " << merge.GetResult().GetLeafsCount() << "\n";
  std::cout << "DAG nodes(without trimming): " << merge.GetResult().GetNodesCount() << "\n";
  std::cout << "DAG edges(without trimming): " << merge.GetResult().GetEdgesCount() << "\n";

  if (trim) {
    merge.ComputeResultEdgeMutations();
    SubtreeWeight<BinaryParsimonyScore, MergeDAG> weight{merge.GetResult()};
    if (sample_tree) {
      std::cout << "sampling a tree from the minweight options\n";
      StoreDAGToProtobuf(weight.MinWeightSampleTree({}).View(), out_path);
    } else {
      StoreDAGToProtobuf(weight.TrimToMinWeight({}).View(), out_path);
    }
  } else {
    merge.ComputeResultEdgeMutations();
    if (sample_tree) {
      std::cout << "sampling a tree from the merge DAG\n";
        SubtreeWeight<BinaryParsimonyScore, MergeDAG> weight{merge.GetResult()};
        StoreDAGToProtobuf(weight.SampleTree({}).View(), out_path);
    } else {
        StoreDAGToProtobuf(merge.GetResult(), out_path);
    }
  }


  return EXIT_SUCCESS;
}

int main(int argc, char** argv) try {
  Arguments args = GetArguments(argc, argv);

  std::vector<std::string_view> input_filenames;
  std::string result_filename = "merged.pb";
  std::string refseq_filename;
  bool dags = false;
  bool trim = false;
  bool sample_tree = false;

  for (auto [name, params] : args) {
    if (name == "-h" or name == "--help") {
      Usage();
    } else if (name == "-i" or name == "--input") {
      ranges::actions::push_back(input_filenames, params);
    } else if (name == "-o" or name == "--output") {
      if (params.empty()) {
        std::cerr << "Specify result file name.\n";
        Fail();
      }
      result_filename = *params.begin();
    } else if (name == "-r" or name == "--refseq") {
      if (params.empty()) {
        std::cerr << "Specify reference sequence Json file name.\n";
        Fail();
      }
      refseq_filename = *params.begin();
    } else if (name == "-d" or name == "--dag") {
      dags = true;
    } else if (name == "-t" or name == "--trim") {
      trim = true;
    } else if (name == "-s" or name == "--sample") {
      sample_tree = true;
    }
  }

  if (input_filenames.size() < 1) {
    std::cerr << "Specify at least one input file names.\n";
    Fail();
  }
  for (auto pth: input_filenames){
    std::cout << pth << "  to be merged\n";
  }

  return MergeTrees(input_filenames, refseq_filename, result_filename, dags, trim, sample_tree);
} catch (std::exception& e) {
  std::cerr << "Uncaught exception: " << e.what() << std::endl;
  std::terminate();
} catch (...) {
  std::abort();
}
