#include <sys/stat.h>
#include <unistd.h>
#include <wcl/filepath.h>

#include <fstream>
#include <map>
#include <random>
#include <sstream>
#include <vector>

#include "job_cache/job_cache.h"
#include "unit.h"
#include "wcl/xoshiro_256.h"

// Later features
// 1) Add a mode for testing without eviction, demanding everything is a hit
// 2) Add a mode where lru is used but hits are still demanded because the size cap is too high to
// hit 3) Add a mode where the same job_cache is being hit by multiple threads (but outputting to
// unique locations) 4) Primary keys would be pulled from a pool (that periodically changes and
// mutates) 5) Files would be pulled from a pool of sources (that periodically mutates) and outputs
// (fed by jobs themselves) 6) Jobs would be pulled from a pool like they are not but all 3 pools
// would be shared across multiple threads

// Primary key normally doesn't change as often as
// input files. Because I'm trying to make some parts of
// this code uniform this probability didn't fit in very well
// so its going to go here as a global instead.
constexpr double primary_key_mutate_prob = 0.05;

// TODO: Implement pool methods for TestFile
struct TestFile {
  std::string path;
  std::string content;
};

struct TestJob {
  std::string cwd;
  std::string cmd;
  std::string env;
  std::string stdin;
  std::vector<TestFile> input_files;
  std::vector<TestFile> output_files;

  job_cache::AddJobRequest generate_add_request(const std::string& in_dir) const {
    JAST request(JSON_OBJECT);
    request.add("cwd", cwd);
    request.add("command_line", cmd);
    request.add("envrionment", env);
    request.add("stdin", stdin);
    // TODO: add options for stdout and stderr
    //       and check them in the test
    request.add("stdout", "");
    request.add("stderr", "");
    // TODO: Add option for status
    request.add("status", 0);
    // TODO: Add options for usage
    request.add("runtime", 1.0);
    request.add("cputime", 1.0);
    request.add("mem", 1024);
    request.add("ibytes", 1024);
    request.add("obytes", 1024);

    char path_buf[4096];
    assert(getcwd(path_buf, sizeof(path_buf)) != NULL);
    request.add("client_cwd", path_buf);

    // Add the input files
    JAST inputs(JSON_ARRAY);
    for (const auto& file : input_files) {
      job_cache::InputFile json_file;
      json_file.path = wcl::join_paths("/workspace", file.path);
      json_file.hash = Hash256::blake2b(file.content);
      inputs.add("", json_file.to_json());
    }
    request.add("input_files", std::move(inputs));

    JAST outputs(JSON_ARRAY);
    for (const auto& file : output_files) {
      std::string src = wcl::join_paths(in_dir, file.path);
      std::ofstream out(src);
      out << file.content;
      out.close();

      job_cache::OutputFile json_file;
      json_file.path = wcl::join_paths("/workspace", file.path);
      json_file.hash = Hash256::blake2b(file.content);
      json_file.source = src;
      json_file.mode = 0664;
      outputs.add("", json_file.to_json());
    }
    request.add("output_files", std::move(outputs));

    return job_cache::AddJobRequest(request);
  }

  job_cache::FindJobRequest generate_find_request(const std::string& out_dir) const {
    JAST request(JSON_OBJECT);
    request.add("cwd", cwd);
    request.add("command_line", cmd);
    request.add("envrionment", env);
    request.add("stdin", stdin);

    char path_buf[4096];
    assert(getcwd(path_buf, sizeof(path_buf)) != NULL);
    request.add("client_cwd", path_buf);

    JAST inputs(JSON_ARRAY);
    for (const auto& file : input_files) {
      JAST json_file(JSON_OBJECT);
      json_file.add("path", wcl::join_paths("/workspace", file.path));
      auto hash = Hash256::blake2b(file.content);
      json_file.add("hash", hash.to_hex());
      inputs.add("", std::move(json_file));
    }
    request.add("input_files", std::move(inputs));

    JAST redirect(JSON_OBJECT);
    redirect.add("/workspace", out_dir);
    request.add("dir_redirects", std::move(redirect));

    return job_cache::FindJobRequest(request);
  }

  static TestJob gen(wcl::xoshiro_256& gen) {
    TestJob out;

    // Generate our primary key
    out.cwd = "/workspace";
    out.cmd = gen.unique_name();
    out.env = gen.unique_name();
    out.stdin = gen.unique_name();

    // Generate our input and output files
    std::uniform_int_distribution<> number_of_inputs_dist(0, 5);
    std::uniform_int_distribution<> number_of_outputs_dist(0, 5);
    int number_of_inputs = number_of_inputs_dist(gen);
    int number_of_outputs = number_of_outputs_dist(gen);

    // TODO: Pull these from input and output pools
    //       This will require an auxiliery type of some
    //       kind.
    for (int i = 0; i < number_of_inputs; ++i) {
      out.input_files.emplace_back();
      out.input_files.back().path = gen.unique_name();
      out.input_files.back().content = gen.unique_name();
    }
    for (int i = 0; i < number_of_outputs; ++i) {
      out.output_files.emplace_back();
      out.output_files.back().path = gen.unique_name();
      out.output_files.back().content = gen.unique_name();
    }

    return out;
  }

  static void mutate(TestJob& to_mutate, wcl::xoshiro_256& gen) {
    std::uniform_real_distribution<> dist(0.0, 1.0);
    if (dist(gen) < primary_key_mutate_prob) {
      std::uniform_int_distribution<> case_dist(1, 3);
      switch (case_dist(gen)) {
        case 1:
          to_mutate.cmd = gen.unique_name();
          return;
        case 2:
          to_mutate.env = gen.unique_name();
          return;
        case 3:
          to_mutate.stdin = gen.unique_name();
          return;
      }
      return;
    }

    std::uniform_int_distribution<> num_to_mutate(1, 3);
    int inputs_to_mutate = num_to_mutate(gen);
    int outputs_to_mutate = num_to_mutate(gen);

    // If there are no input files then the following loop wont enter.
    // but also it isn't valid to change the outputs without changing
    // and input as well so we just return early in that case
    if (to_mutate.input_files.empty()) return;

    for (int i = 0; i < inputs_to_mutate; ++i) {
      std::uniform_int_distribution<> num_to_mutate(0, to_mutate.input_files.size() - 1);
      to_mutate.input_files[num_to_mutate(gen)].content = gen.unique_name();
    }

    if (to_mutate.output_files.empty()) return;
    for (int i = 0; i < outputs_to_mutate; ++i) {
      std::uniform_int_distribution<> num_to_mutate(0, to_mutate.output_files.size() - 1);
      to_mutate.output_files[num_to_mutate(gen)].content = gen.unique_name();
    }
  }
};

// TODO: Make Pool thread-safe
template <class T>
struct Pool {
  std::vector<T> pool;

  double reuse_prob = 0.95;
  double mutate_prob = 0.1;
  double delete_prob = 0.14;  // delete_prob = 1 - reuse_prob + reuse_prob*mutate_prob
  static constexpr int reuse_threshold = 5;

  Pool(double reuse_prob = 0.95, double mutate_prob = 0.1)
      : reuse_prob(reuse_prob),
        mutate_prob(mutate_prob),
        delete_prob(1 - reuse_prob + reuse_prob * mutate_prob) {}

  // TODO: When this is thread safe, note that this will assume read access
  T& reuse(wcl::xoshiro_256& gen) {
    size_t last_idx = pool.size() - 1;
    std::uniform_int_distribution<size_t> index_to_reuse_dist(0, last_idx);
    size_t idx = index_to_reuse_dist(gen);
    return pool[idx];
  }

  // TODO: When this is thread safe, note that this will assume write access
  void remove(wcl::xoshiro_256& gen) {
    size_t last_idx = pool.size() - 1;
    std::uniform_int_distribution<size_t> index_to_remove_dist(0, last_idx);
    size_t index_to_remove = index_to_remove_dist(gen);
    std::swap(pool[last_idx], pool[index_to_remove]);
    pool.pop_back();
  }

  // TODO: When this is thread safe
  //       1) It will need to return a copy not a reference
  //       2) It will need to aquire the read/write lock early
  const T& step(wcl::xoshiro_256& gen) {
    std::uniform_real_distribution<> dist(0.0, 1.0);

    // If we're empty we can't reuse anything
    if (pool.size() <= reuse_threshold) {
      pool.emplace_back(T::gen(gen));
      return pool.back();
    }

    // Make modifications to the pool
    if (dist(gen) < delete_prob) {
      remove(gen);
    }

    // Check if we want to reuse an existing job
    if (dist(gen) < reuse_prob) {
      T& to_reuse = reuse(gen);
      if (dist(gen) < mutate_prob) {
        T to_mutate = to_reuse;
        T::mutate(to_mutate, gen);
        pool.emplace_back(to_mutate);
        return pool.back();
      }
      return to_reuse;
    }

    // Otherwise we need to generate a new thing and add it to the pool
    pool.emplace_back(T::gen(gen));
    return pool.back();
  }
};

TEST_FUNC(void, fuzz_loop, size_t number_of_steps, std::string cache_dir, std::string dir,
          wcl::xoshiro_256 gen) {
  Pool<TestJob> job_pool;

  mkdir(cache_dir.c_str(), 0777);
  mkdir(dir.c_str(), 0777);
  job_cache::Cache cache(cache_dir, 1ULL << 24ULL, (1 << 23ULL) + (1 << 22ULL));

  std::string out_dir = wcl::join_paths(dir, "outputs");
  for (size_t i = 0; i < number_of_steps; ++i) {
    // First find the job that we care about
    const TestJob& job = job_pool.step(gen);
    auto find_job_request = job.generate_find_request(out_dir);
    auto result = cache.read(find_job_request);
    if (result.match) {
      for (auto file : job.output_files) {
        std::ifstream t(wcl::join_paths(out_dir, file.path));
        std::stringstream buffer;
        buffer << t.rdbuf();
        ASSERT_EQUAL(buffer.str(), file.content);
      }
    } else {
      auto add_job_request = job.generate_add_request(dir);
      cache.add(add_job_request);
    }
  }
}

TEST(job_cache_basic_fuzz) {
  wcl::xoshiro_256 gen(wcl::xoshiro_256::get_rng_seed());
  TEST_FUNC_CALL(fuzz_loop, 10000, ".job_cache_test", "job_cache_test", std::move(gen));
}
