#include <json/json5.h>
#include <sched.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <wcl/filepath.h>
#include <wcl/tracing.h>

#include <fstream>
#include <future>
#include <map>
#include <random>
#include <set>
#include <sstream>
#include <thread>
#include <vector>

#include "job_cache/job_cache.h"
#include "unit.h"
#include "util/mkdir_parents.h"
#include "wcl/filepath.h"
#include "wcl/xoshiro_256.h"

// Set sane defaults to avoid subtle errors
struct FuzzLoopConfig {
  int max_vis = 5;
  int max_out = 5;
  int max_path_size = 16;
  size_t number_of_steps = 1;
  std::string cache_dir;
  std::string dir;
};

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

std::string generate_long_string(char sep, std::string seed, int target_size) {
  while (seed.size() <= size_t(target_size)) {
    auto copy = seed;
    seed += sep;
    seed += copy;
  }
  seed.resize(target_size);
  return seed;
}

struct TestJob {
  std::string cwd;
  std::string cmd;
  std::string env;
  std::string stdin;
  std::vector<TestFile> input_files;
  std::vector<TestFile> output_files;

  job_cache::AddJobRequest generate_add_request(const std::string& in_dir) const {
    JAST request(JSON_OBJECT);
    request.add("wakeroot", cwd);
    request.add("cwd", ".");
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
      auto pab = wcl::parent_and_base(src);
      std::string parent_dir = pab->first;
      mkdir_with_parents(parent_dir, 0777);
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
    request.add("wakeroot", cwd);
    request.add("cwd", ".");
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

  static TestJob gen(const FuzzLoopConfig& config, wcl::xoshiro_256& gen) {
    TestJob out;

    // Generate our primary key
    out.cwd = "/workspace";
    out.cmd = gen.unique_name();
    out.env = gen.unique_name();
    out.stdin = gen.unique_name();

    // Generate our input and output files
    std::uniform_int_distribution<> number_of_inputs_dist(0, config.max_vis);
    std::uniform_int_distribution<> number_of_outputs_dist(0, config.max_out);
    std::uniform_int_distribution<> file_path_size(16, config.max_path_size);
    int number_of_inputs = number_of_inputs_dist(gen);
    int number_of_outputs = number_of_outputs_dist(gen);

    // TODO: Pull these from input and output pools
    //       This will require an auxiliery type of some
    //       kind.
    for (int i = 0; i < number_of_inputs; ++i) {
      out.input_files.emplace_back();
      out.input_files.back().path =
          generate_long_string('/', gen.unique_name(), file_path_size(gen));
      out.input_files.back().content = gen.unique_name();
    }
    for (int i = 0; i < number_of_outputs; ++i) {
      out.output_files.emplace_back();
      out.output_files.back().path =
          generate_long_string('/', gen.unique_name(), file_path_size(gen));
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
  template <class... Args>
  const T& step(wcl::xoshiro_256& gen, Args&&... gen_args) {
    std::uniform_real_distribution<> dist(0.0, 1.0);

    // If we're empty we can't reuse anything
    if (pool.size() <= reuse_threshold) {
      pool.emplace_back(T::gen(std::forward<Args>(gen_args)..., gen));
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
    pool.emplace_back(T::gen(std::forward<Args>(gen_args)..., gen));
    return pool.back();
  }
};

template <class F>
bool run_as_init_proc(F f) {
  // We're going to unshare some namespaces but this can mess with later
  // things that spawn threads so we want to fork first to isolate
  int pid_wrapper = fork();

  if (pid_wrapper == -1) {
    wcl::log::error("run_as_init_proc: fork failure: %s", strerror(errno))();
    return false;
  }

  // We wait for the child to return here
  if (pid_wrapper != 0) {
    int status = 0;
    if (waitpid(pid_wrapper, &status, 0) != pid_wrapper) {
      wcl::log::error("run_as_init_proc: waitpid(): %s", strerror(errno))();
      return false;
    }

    // Relay errors up to the top
    if (WIFEXITED(status) && WEXITSTATUS(status) == 0) {
      return true;
    }
    return false;
  }

  // Now that we're in an isolated child, we unshare our namespaces
  if (unshare(CLONE_NEWUSER | CLONE_NEWNS | CLONE_NEWPID) != 0) {
    wcl::log::error("unshare(CLONE_NEW_USER | CLONE_NEWNS | CLONE_NEWPID): %s", strerror(errno))();
    exit(1);
  }

  // Now the next child we create will be the init process for that namespace.
  int pid = fork();

  if (pid == -1) {
    wcl::log::error("run_as_init_proc: fork failure: %s", strerror(errno))();
    exit(1);
  }

  // In the child process we just want to run the function.
  // We assure the user that the code executed by f() will be
  // executed as the init process of its own namespace.
  if (pid == 0) {
    // mount a new proc filesystem in the init process
    if (mount("proc", "/proc", "proc", 0, nullptr) == -1) {
      wcl::log::error("mount(proc): %s", strerror(errno))();
      exit(1);
    }

    int retcode = f();
    wcl::log::info("exiting process: retcode = %d", retcode)();
    exit(retcode);
  }

  // We now need to wait on f() to finish to simulate this interface
  // as being sync
  int status = 0;
  if (waitpid(pid, &status, 0) != pid) {
    wcl::log::error("run_as_init_proc: waitpid(): %s", strerror(errno))();
    exit(1);
  }

  // Relay the errors up to the top
  if (WIFEXITED(status)) {
    exit(WEXITSTATUS(status));
  }
  exit(2);
}

void signal_and_wait(pid_t pid, int sig, int us) {
  kill(pid, sig);
  usleep(us);
}

TEST_FUNC(void, fuzz_loop, const FuzzLoopConfig& config, wcl::xoshiro_256 gen) {
  Pool<TestJob> job_pool;

  mkdir(config.cache_dir.c_str(), 0777);
  mkdir(config.dir.c_str(), 0777);
  job_cache::Cache cache(config.cache_dir, 1ULL << 24ULL, (1 << 23ULL) + (1 << 22ULL), false);

  std::string out_dir = wcl::join_paths(config.dir, "outputs");
  for (size_t i = 0; i < config.number_of_steps; ++i) {
    // First find the job that we care about
    const TestJob& job = job_pool.step(gen, config);
    auto find_job_request = job.generate_find_request(out_dir);
    auto result = cache.read(find_job_request);
    if (result.match) {
      for (auto file : job.output_files) {
        std::ifstream t(wcl::join_paths(out_dir, file.path));
        std::stringstream buffer;
        buffer << t.rdbuf();
        // NOTE: We avoid asserting because it causes wild things to happen
        // when we use fuzz_many_with_ns because it causes child processes
        // to long jump back to main and exit.
        EXPECT_EQUAL(buffer.str(), file.content);
      }
    } else {
      auto add_job_request = job.generate_add_request(config.dir);
      cache.add(add_job_request);
    }
  }
}

TEST_FUNC(void, fuzz_many_with_ns, int num_procs, const FuzzLoopConfig& config,
          wcl::xoshiro_256 gen) {
  // This will look a bit odd because some of the logs will be from outside the pid namespace
  // and some will be from inside the pid namespace but we'll just have to deal with that.
  auto res = JsonSubscriber::fd_t::open("wake.log");
  ASSERT_TRUE((bool)res) << "Unable to init logging: wake.log failed to open: " << strerror(res.error());
  wcl::log::subscribe(std::make_unique<JsonSubscriber>(std::move(*res)));

  // Note that there will be processes in the o
  bool result = run_as_init_proc([&]() -> int {
    // We want to keep a certain number of processes
    // unkilled, we call these procs "immune"
    std::set<pid_t> immune_procs;
    for (int i = 0; i < num_procs; ++i) {
      pid_t pid = fork();
      if (pid == -1) {
        wcl::log::error("fuzz_many_with_ns: fork failure: %s", strerror(errno))();
        return 1;
      }

      if (pid == 0) {
        wcl::log::info("test proc was forked!")();
        // We need to construct a different generator so the jobs aren't stomping on each other
        TEST_FUNC_CALL(fuzz_loop, config, wcl::xoshiro_256(wcl::xoshiro_256::get_rng_seed()));
        wcl::log::info("process exiting naturally: num_errors = %d", (int)NUM_ERRORS())();
        exit(NUM_ERRORS() != 0);
      }
      if (i <= num_procs / 2) {
        immune_procs.insert(pid);
      }
    }

    // Now we need to loop over children and randomly kill the
    // the non-immune ones. We should exit once we have no children
    // left.
    bool child_found = false;
    while (child_found) {
      child_found = false;
      auto proc_dir = wcl::directory_range::open("/proc");
      if (!proc_dir) {
        wcl::log::error("unable to open /proc")();
        return 1;
      }

      // This loop is the chaos monkey that randomly tampers with processes
      for (const auto& entry : *proc_dir) {
        if (!entry) {
          wcl::log::error("entry error in /proc: %s", strerror(entry.error()))();
          return 1;
        }
        // Only the entries that are numbers are processes
        if (entry->name.size() == 0) continue;
        if (!isdigit(entry->name[0])) continue;
        wcl::log::info("checking on pid = %s", entry->name.c_str())();
        pid_t pid = std::stoi(entry->name);

        // We should see ourselves in this namespace, and we are process 1
        // so skip ourselves
        if (pid == 1) continue;

        switch (gen() & 3) {
          case 0:
            break;
          case 1:
            if (!immune_procs.count(pid)) signal_and_wait(pid, SIGKILL, 5000);
            break;
          case 2:
            // If there's only one process left running, we don't want to keep pausing it,
            // otherwise we'll never make progress.
            if (!child_found) break;
            signal_and_wait(pid, SIGSTOP, 1000);
            signal_and_wait(pid, SIGCONT, 0);
            break;
          case 3:
            if (!immune_procs.count(pid)) signal_and_wait(pid, SIGTERM, 5000);
            break;
        }

        // Lastly we want to end the loop once we don't have any more processes running
        child_found = true;
      }
    }

    // Now we need to collect the status of all the jobs. We had each job exit 0 if it
    // found no errors so we'll check that here.
    while (true) {
      int status = 0;
      pid_t pid = waitpid(-1, &status, 0);
      if (pid == -1 && errno == ECHILD) {
        wcl::log::error("no more children! return 0")();
        return 0;
      }
      if (pid == -1) {
        wcl::log::error("fuzz_many_ns: waitpid: %s", strerror(errno))();
        return 1;
      }
      if (WIFEXITED(status) && WEXITSTATUS(status) != 0) {
        wcl::log::error("fuzz_many_ns: process with pid = %d, exited with retcode = %d", pid,
                        WEXITSTATUS(status))();
        return 1;
      }
      if (!WIFEXITED(status)) {
        wcl::log::info("pid = %d had a non-exit waitpid event", pid)();
      } else {
        wcl::log::info("pid = %d exited with return code 0", pid)();
      }
    }
  });

  // This is a very low-information problem unfortunately,
  ASSERT_TRUE(result) << "choas testing for shared cache failed: check logs for issues";
}

TEST(job_cache_basic_fuzz) {
  wcl::xoshiro_256 gen(wcl::xoshiro_256::get_rng_seed());
  FuzzLoopConfig config;
  config.max_path_size = 16;
  config.max_out = 5;
  config.max_vis = 5;
  config.number_of_steps = 10000;
  config.cache_dir = ".job_cache_test";
  config.dir = "job_cache_test";
  TEST_FUNC_CALL(fuzz_loop, config, std::move(gen));
}

TEST(job_cache_par_chaos_fuzz, "pid-namespace") {
  wcl::xoshiro_256 gen(wcl::xoshiro_256::get_rng_seed());
  FuzzLoopConfig config;
  config.max_path_size = 16;
  config.max_out = 5;
  config.max_vis = 5;
  config.number_of_steps = 10000;
  config.cache_dir = ".job_cache_test_chaos";
  config.dir = "job_cache_test_chaos";
  TEST_FUNC_CALL(fuzz_many_with_ns, 20, config, std::move(gen));
}

// This test appears to work but it takes quite a long time and
// causes a lot of filesystem churn. Just test this on your own
// occasionally as a debugging/repro tool for those kinds of issues.

TEST(job_cache_large_message_fuzz, "large") {
  wcl::xoshiro_256 gen(wcl::xoshiro_256::get_rng_seed());
  FuzzLoopConfig config;
  config.max_path_size = 200;
  config.max_out = 16000;
  config.max_vis = 16000;
  config.number_of_steps = 100;
  config.cache_dir = ".job_cache_test_large";
  config.dir = "job_cache_test_large";
  TEST_FUNC_CALL(fuzz_loop, config, std::move(gen));
}

// This test appears to work but it takes *FOREVER* and doesn't represent
// a very likely case. Still it might be worth running this on your own
// sometimes to make sure everything is working well

TEST(job_cache_huge_message_fuzz, "huge") {
  wcl::xoshiro_256 gen(wcl::xoshiro_256::get_rng_seed());
  FuzzLoopConfig config;
  config.max_path_size = 3500;
  config.max_out = 100000;
  config.max_vis = 100000;
  config.number_of_steps = 20;
  config.cache_dir = ".job_cache_test_huge";
  config.dir = "job_cache_test_huge";
  TEST_FUNC_CALL(fuzz_loop, config, std::move(gen));
}

TEST(job_cache_basic_par_fuzz, "threaded") {
  FuzzLoopConfig config;
  config.max_path_size = 16;
  config.max_out = 5;
  config.max_vis = 5;
  config.number_of_steps = 500;
  config.cache_dir = ".job_cache_test2";
  config.dir = "job_cache_test2";
  std::vector<std::future<void>> futs;
  for (int i = 0; i < 20; ++i) {
    // Each thread will exit on ASSERT fail logging the error
    // and will correctly log failed EXPECTS. Because we wait
    // on all futures in this test there is no way for this to
    // leave a thread running after the return of this call.
    // However it is unfortunate that if one thread fails,
    // these others will keep running to completion. Additionally
    // if the program dies/crashes all threads die in the current
    // position without failures from other threads being logged.
    futs.emplace_back(std::async([&]() {
      wcl::xoshiro_256 gen(wcl::xoshiro_256::get_rng_seed());
      TEST_FUNC_CALL(fuzz_loop, config, std::move(gen));
    }));
  }
  for (auto& fut : futs) {
    if (fut.valid()) fut.wait();
  }
}

// This test should work but it takes quite a long time.
TEST(job_cache_large_par_fuzz, "large") {
  FuzzLoopConfig config;
  config.max_path_size = 16;
  config.max_out = 5;
  config.max_vis = 5;
  config.number_of_steps = 1000;
  config.cache_dir = ".job_cache_test";
  config.dir = "job_cache_test";
  std::vector<std::future<void>> futs;
  for (int i = 0; i < 500; ++i) {
    // Each thread will exit on ASSERT fail logging the error
    // and will correctly log failed EXPECTS. Because we wait
    // on all futures in this test there is no way for this to
    // leave a thread running after the return of this call.
    // However it is unfortunate that if one thread fails,
    // these others will keep running to completion. Additionally
    // if the program dies/crashes all threads die in the current
    // position without failures from other threads being logged.
    futs.emplace_back(std::async([&]() {
      wcl::xoshiro_256 gen(wcl::xoshiro_256::get_rng_seed());
      TEST_FUNC_CALL(fuzz_loop, config, std::move(gen));
    }));
  }
  for (auto& fut : futs) {
    if (fut.valid()) fut.wait();
  }
}
