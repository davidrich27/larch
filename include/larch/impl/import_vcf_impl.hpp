// Modified from deps/usher/matOptimize/import_vcf_fast.cpp

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wold-style-cast"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wdeprecated-copy-with-user-provided-copy"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wdeprecated-copy"
#pragma GCC diagnostic ignored "-Wextra"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wconversion"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
// #include "src/matOptimize/import_vcf.hpp"
#include "src/matOptimize/mutation_annotated_tree.hpp"
#include "src/matOptimize/tree_rearrangement_internal.hpp"
#pragma GCC diagnostic pop

#include "tbb/concurrent_queue.h"
#include "tbb/flow_graph.h"
#include "tbb/parallel_for.h"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <fstream>
#include <iostream>
#include <mutex>
#include <signal.h>
#include <string>
#include <sys/mman.h>
#include <sys/stat.h>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#define ZLIB_BUFSIZ 0x10000
size_t read_size;
size_t alloc_size;
std::ifstream fp;

// Decouple parsing (slow) and decompression, segment file into blocks for
// parallelized parsing
typedef tbb::flow::source_node<char *> decompressor_node_t;
std::condition_variable progress_bar_cv;

struct raw_input_source {
  FILE *fh;
  raw_input_source(const char *fname) {
    fh = fopen(fname, "r");
    fp.open(fname);
  }
  int getc() { return fgetc(fh); }
  void operator()(
      tbb::concurrent_bounded_queue<std::pair<char *, uint8_t *>> &out) const {
    std::string fp_line;
    // while (std::getline(fp, fp_line)) {
    //   std::cout << "LINE: " << std::endl;
    //   std::cout << fp_line << std::endl;
    // }

    while (!feof(fh)) {
      auto line_out = new char[alloc_size];
      auto bytes_read = fread(line_out, 1, read_size, fh);
      if (bytes_read == 0) {
        delete[] line_out;
        break;
      }

      *(line_out + bytes_read) = '\0';
      out.emplace(line_out, (uint8_t *)line_out + bytes_read);
    }
    out.emplace(nullptr, nullptr);
    out.emplace(nullptr, nullptr);
  }
  void unalloc() { fclose(fh); }
};
struct line_start_later {
  char *start;
  char *alloc_start;
};
struct line_align {
  mutable line_start_later prev;
  mutable uint8_t *prev_end;
  tbb::concurrent_bounded_queue<std::pair<char *, uint8_t *>> &in;
  line_align(tbb::concurrent_bounded_queue<std::pair<char *, uint8_t *>> &out)
      : in(out) {
    prev.start = nullptr;
  }
  bool operator()(line_start_later &out) const {
    std::pair<char *, unsigned char *> line;
    in.pop(line);
    if (line.first == nullptr) {
      if (prev.start != nullptr) {
        out = prev;
        prev.start = nullptr;
        prev_end[0] = 0;
        return true;
      } else {
        return false;
      }
    }
    if (!prev.start) {
      prev.start = line.first;
      prev.alloc_start = line.first;
      prev_end = line.second;
      in.pop(line);
      if (line.first == nullptr) {
        out = prev;
        prev.start = nullptr;
        return true;
      }
    }
    auto start_ptr = strchr(line.first, '\n');
    if (*start_ptr != '\n') {
      raise(SIGTRAP);
    }
    start_ptr++;
    auto cpy_siz = start_ptr - line.first;
    memcpy(prev_end, line.first, cpy_siz);
    prev_end[cpy_siz] = 0;
    out = prev;
    prev.start = start_ptr;
    prev.alloc_start = line.first;
    prev_end = line.second;
    return true;
  }
};
//
// Parse a block of lines, assuming there is a complete line in the line_in
// buffer
typedef tbb::flow::multifunction_node<line_start_later,
                                      tbb::flow::tuple<Parsed_VCF_Line *>>
    line_parser_t;

struct line_parser {
  const std::vector<long> &header;
  void operator()(line_start_later line_in_struct,
                  line_parser_t::output_ports_type &out) const {
    char *line_in = line_in_struct.start;
    char *to_free = line_in_struct.alloc_start;
    Parsed_VCF_Line *parsed_line;

    while (*line_in != '\0') {
      std::vector<nuc_one_hot> allele_translated;
      std::string chromosome;
      int pos = 0;
      // CHROME
      while (*line_in != '\t') {
        chromosome.push_back(*line_in);
        line_in++;
      }
      line_in++;
      // POS
      while (*line_in != '\t') {
        pos = pos * 10 + (*line_in - '0');
        line_in++;
      }
      if (pos <= 0) {
        raise(SIGTRAP);
      }
      line_in++;
      // ID (don't care)
      while (*line_in != '\t') {
        line_in++;
      }
      line_in++;
      // REF
      parsed_line = new Parsed_VCF_Line{
          MAT::Mutation(chromosome, pos, 0, 0, 1, MAT::get_nuc_id(*line_in))};
      auto &non_ref_muts_out = parsed_line->mutated;
      line_in++;
      // assert(*line_in=='\t');
      line_in++;
      // ALT
      while (*line_in != '\t') {
        allele_translated.push_back(MAT::get_nuc_id(*line_in));
        line_in++;
        if (*line_in == ',') {
          line_in++;
        } else {
          // assert(*line_in=='\t');
        }
      }
      line_in++;

      // QUAL, FILTER, INFO, FORMAT
      unsigned int field_idx = 5;
      for (; field_idx < 9; field_idx++) {
        while (*line_in != '\t') {
          line_in++;
        }
        line_in++;
      }
      // SAMPLES
      bool is_last = false;
      while (!is_last) {
        unsigned int allele_idx = (*line_in - '0');
        line_in++;
        while (std::isdigit(*line_in)) {
          allele_idx *= 10;
          allele_idx += (*line_in - '0');
          line_in++;
        }

        while (*line_in != '\t') {
          if (*line_in == '\n' || *line_in == '\0') {
            is_last = true;
            break;
          }
          line_in++;
        }
        if (header[field_idx] > 0) {
          // output prototype of mutation, and a map from sample to
          // non-ref allele
          if (allele_idx >= (allele_translated.size() + 1)) {
            // ambiguity character.
            non_ref_muts_out.emplace_back(header[field_idx], 0xf);
          } else if (allele_idx && header[field_idx] >= 0) {
            non_ref_muts_out.emplace_back(header[field_idx],
                                          allele_translated[allele_idx - 1]);
          }
        }
        field_idx++;
        line_in++;
      }
      // assert(field_idx==header.size());
      std::sort(non_ref_muts_out.begin(), non_ref_muts_out.end(),
                mutated_t_comparator());
      non_ref_muts_out.emplace_back(0, 0);

      {
        // std::cout << "# non_refs_muts_out: [ ";
        // for (const auto &[pos, nuc_one_hot] : non_ref_muts_out) {
        //   std::cout << "( " << pos << " " << std::to_string(nuc_one_hot.nuc) << " )
        //   ";
        // }
        // std::cout << "] " << std::endl;
      }

      std::get<0>(out).try_put(parsed_line);
    }
    delete[](to_free);
  }
};
// tokenize header, get sample name
template <typename infile_t>
static void read_header(infile_t &fd, std::vector<std::string> &out) {
  char in = fd.getc();
  in = fd.getc();
  bool second_char_pong = (in == '#');

  while (second_char_pong) {
    while (in != '\n') {
      in = fd.getc();
    }
    in = fd.getc();
    in = fd.getc();
    second_char_pong = (in == '#');
  }

  bool eol = false;
  while (!eol) {
    std::string field;
    while (in != '\t') {
      if (in == '\n') {
        eol = true;
        break;
      }
      field.push_back(in);
      in = fd.getc();
    }
    if (!eol) {
      in = fd.getc();
    }
    out.push_back(field);
  }
}

std::atomic<size_t> assigned_count;
struct Assign_State {
  const std::vector<backward_pass_range> &child_idx_range;
  const std::vector<forward_pass_range> &parent_idx;
  FS_result_per_thread_t &output;
  void operator()(const Parsed_VCF_Line *vcf_line) const {
    auto &this_out = output.local();
    this_out.init(child_idx_range.size());
    assert(vcf_line->mutation.get_position() > 0);
    Fitch_Sankoff_Whole_Tree(child_idx_range, parent_idx, vcf_line->mutation,
                             vcf_line->mutated, this_out);
    assigned_count.fetch_add(1, std::memory_order_relaxed);
    delete vcf_line;
  }
};

void print_progress(std::atomic<bool> *done, std::mutex *done_mutex) {
  while (true) {
    {
      std::unique_lock<std::mutex> lk(*done_mutex);
      progress_bar_cv.wait_for(lk, std::chrono::seconds(1));
      if (done->load()) {
        return;
      }
    }
    fprintf(stderr, "\rAssigned %zu locus",
            assigned_count.load(std::memory_order_relaxed));
  }
}

template <typename infile_t>
static line_start_later try_get_first_line(infile_t &f, size_t &size) {
  std::string temp;
  char c;
  while ((c = f.getc()) != '\n') {
    temp.push_back(c);
  }
  temp.push_back('\n');
  size = temp.size() + 1;
  char *buf = new char[size];
  strcpy(buf, temp.data());
  return line_start_later{buf, buf};
}

#define CHUNK_SIZ 200ul
#define ONE_GB 0x4ffffffful

template <typename infile_t>
static void process(MAT::Tree &tree, infile_t &fd) {
  std::vector<std::string> fields;
  read_header(fd, fields);
  std::vector<MAT::Node *> bfs_ordered_nodes = tree.breadth_first_expansion();
  std::vector<long> idx_map(9);
  {
    std::unordered_set<std::string> inserted_samples;
    for (size_t idx = 9; idx < fields.size(); idx++) {
      auto node = tree.get_node(fields[idx]);
      ;
      if (node == nullptr) {
        fprintf(stderr, "sample %s cannot be found in tree\n", fields[idx].c_str());
        idx_map.push_back(-1);
        // exit(EXIT_FAILURE);
      } else {
        auto res = inserted_samples.insert(fields[idx]);
        if (res.second) {
          idx_map.push_back(node->bfs_index);
        } else {
          idx_map.push_back(-1);
        }
      }
    }
  }
  {
    std::vector<backward_pass_range> child_idx_range;
    std::vector<forward_pass_range> parent_idx;
    FS_result_per_thread_t output;
    Fitch_Sankoff_prep(bfs_ordered_nodes, child_idx_range, parent_idx);
    {
      tbb::flow::graph input_graph;
      line_parser_t parser(input_graph, /* tbb::flow::unlimited */ 1,
                           line_parser{idx_map});
      // feed used buffer back to decompressor
      tbb::flow::function_node<Parsed_VCF_Line *> assign_state(
          input_graph, /* tbb::flow::unlimited */ 1,
          Assign_State{child_idx_range, parent_idx, output});
      tbb::flow::make_edge(tbb::flow::output_port<0>(parser), assign_state);
      size_t single_line_size;
      parser.try_put(try_get_first_line(fd, single_line_size));
      size_t first_approx_size = std::min(CHUNK_SIZ, ONE_GB / single_line_size) - 2;
      read_size = first_approx_size * single_line_size;
      alloc_size = (first_approx_size + 2) * single_line_size;
      tbb::concurrent_bounded_queue<std::pair<char *, uint8_t *>> queue;
      queue.set_capacity(10);
      tbb::flow::source_node<line_start_later> line(input_graph, line_align(queue));
      tbb::flow::make_edge(line, parser);
      // raise(SIGTRAP);
      fd(queue);
      input_graph.wait_for_all();
    }

    {
      // std::cout << "muts: [ ";
      // tbb::enumerable_thread_specific<Fitch_Sankoff_Out_Container>::const_iterator
      // it; for (it = output.begin(); it != output.end(); ++it) {
      //   const auto &thread = *it;
      //   std::cout << "[ ";
      //   for (const auto &muts : thread.output) {
      //     std::cout << "[ ";
      //     for (const auto &mut : muts) {
      //       std::cout << mut.get_string() << " ";
      //     }
      //     std::cout << "] ";
      //   }
      //   std::cout << "] ";
      // }
      // std::cout << "] " << std::endl;
    }

    // deallocate_FS_cache(output);
    fill_muts(output, bfs_ordered_nodes);
  }
  size_t total_mutation_size = 0;
  for (const auto node : bfs_ordered_nodes) {
    total_mutation_size += node->mutations.size();
  }
  fprintf(stderr, "Total mutation size %zu \n", total_mutation_size);
  fd.unalloc();
}

inline void VCF_input(const char *name, MAT::Tree &tree) {
  assigned_count = 0;
  std::atomic<bool> done(false);
  std::mutex done_mutex;
  std::thread progress_meter(print_progress, &done, &done_mutex);
  // open file set increase buffer size
  std::string vcf_filename(name);
  raw_input_source fd(name);
  process(tree, fd);
  done = true;
  progress_bar_cv.notify_all();
  progress_meter.join();
  fprintf(stderr, "Finish vcf input \n");
}
