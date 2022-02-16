/* Copyright 2022 SiFive, Inc.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You should have received a copy of LICENSE.Apache2 along with
 * this software. If not, you may obtain a copy at
 *
 *    https://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <iostream>
#include <algorithm>
#include <map>
#include <fstream>
#include <sstream>
#include <set>

#include "timeline.h"
#include "runtime/database.h"
#include "util/execpath.h"
#include "json/json5.h"

struct JobNode {
    JobReflection &job;
    int64_t starttime;
    int64_t endtime;

    std::set<long> dependencies;

    // Hypothetical times are the times the jobs would have started and ended if:
    // every job without dependencies started at 0;
    // every job with dependencies started as soon as its last dependency ended.
    // The critical path therefore ends with the job with the greatest hypothetical endtime.
    int64_t hypothetical_starttime = 0;
    int64_t hypothetical_endtime = -1;

    long parent_id = -1; // last dependency (has the greatest hypothetical endtime)

    bool is_on_critical_path = false;

    explicit JobNode(JobReflection &_job) : job(_job) {
        std::string start_str = std::to_string(_job.starttime.as_int64());
        std::string end_str = std::to_string(_job.endtime.as_int64());
        starttime = std::stoll(start_str.substr(0, start_str.length() - 6));
        endtime = std::stoll(end_str.substr(0, end_str.length() - 6));
    }
};

void dfs_top_sort(long job_id, std::map<long, JobNode> &job_map, std::vector<long> &top_sorted_jobs, std::set<long> &visited) {
    JobNode &job = job_map.at(job_id);
    visited.insert(job_id);
    for (auto it = job.dependencies.begin(); it != job.dependencies.end(); ) {
        if (job_map.find(*it) == job_map.end()) {
            it = job.dependencies.erase(it);
            continue;
        }
        if (visited.find(*it) == visited.end()) { // if not visited
            dfs_top_sort(*it, job_map, top_sorted_jobs, visited);
        }
        it++;
    }
    top_sorted_jobs.emplace_back(job_id);
}

std::vector<long> top_sort(std::map<long, JobNode> &job_map) {
    std::vector<long> top_sorted_jobs;
    std::set<long> visited;
    for (const auto &job_pair: job_map) {
        if (visited.find(job_pair.first) == visited.end()) { // if not visited
            dfs_top_sort(job_pair.first, job_map, top_sorted_jobs, visited);
        }
    }
    return top_sorted_jobs;
}

long fill_one_dependency(FileAccess access, long dependency, std::map<long, JobNode> &job_map) {
    if (access.type == 2) { // The job that output a file is a dependency of jobs that see the file.
        return access.job; // return new dependency
    }
    if (job_map.find(access.job) != job_map.end()) {
        job_map.at(access.job).dependencies.insert(dependency);
    }
    return dependency; // return the same dependency
}

void fill_all_dependencies(const std::vector<FileAccess> &accesses, std::map<long, JobNode> &job_map) {
    long dependency = -1;
    for (auto access: accesses) {
        dependency = fill_one_dependency(access, dependency, job_map);
    }
}

void assign_parents(std::map<long, JobNode> &job_map, std::vector<long> &top_sorted_jobs) {
    for (long job_id: top_sorted_jobs) {
        JobNode &job_node = job_map.at(job_id);
        for (long dependency_id: job_node.dependencies) {
            JobNode dependency = job_map.at(dependency_id);
            if (dependency.hypothetical_endtime >= job_node.hypothetical_starttime) {
                // A job hypothetically starts as soon as its dependency with the greatest hypothetical endtime ends.
                job_node.hypothetical_starttime = dependency.hypothetical_endtime;
                job_node.parent_id = dependency_id;
            }
        }

        job_node.hypothetical_endtime = job_node.hypothetical_starttime + (job_node.endtime - job_node.starttime);
    }
}

void find_critical_path(std::map<long, JobNode> &job_map, std::vector<long> &critical_path) {
    long latest_job_id = -1;
    for (const auto &job_pair: job_map) {
        if (latest_job_id == -1 ||
            job_map.at(latest_job_id).hypothetical_endtime < job_pair.second.hypothetical_endtime) {
            latest_job_id = job_pair.second.job.job;
        }
    }
    job_map.at(latest_job_id).is_on_critical_path = true;
    critical_path.emplace_back(job_map.at(latest_job_id).job.job);
    while (job_map.at(latest_job_id).parent_id != -1) {
        latest_job_id = job_map.at(latest_job_id).parent_id;
        job_map.at(latest_job_id).is_on_critical_path = true;
        critical_path.emplace_back(job_map.at(latest_job_id).job.job);
    }
}

void dfs_transitive_reduction(long job_id, std::map<long, JobNode> &job_map, std::set<long> &ancestors, std::set<long> &visited) {
    JobNode &job = job_map.at(job_id);
    if (visited.find(job_id) == visited.end()) { // if not visited
        ancestors.insert(job_id); // nodes not yet visited have not been stripped of their unnecessary edges
        visited.insert(job_id);
    }

    for (long child_id: job.dependencies) {
        JobNode &child = job_map.at(child_id);
        for (long grandchild_id: child.dependencies) {
            for (long ancestor_id: ancestors) {
                JobNode ancestor = job_map.at(ancestor_id);
                auto grandchild_ptr = std::find(ancestor.dependencies.begin(), ancestor.dependencies.end(),
                                                grandchild_id);
                if (grandchild_ptr != ancestor.dependencies.end()) {
                    ancestor.dependencies.erase(grandchild_ptr);
                }
            }
        }
        dfs_transitive_reduction(child_id, job_map, ancestors, visited);
    }
    ancestors.erase(job_id);
}

void transitive_reduction(std::map<long, JobNode> &job_map) {
    std::set<long> ancestors, visited;
    for (const auto &job_pair: job_map) {
        if (visited.find(job_pair.first) == visited.end()) { // if not visited
            dfs_transitive_reduction(job_pair.first, job_map, ancestors, visited);
        }
    }
}

void write_jobs(const std::map<long, JobNode> &job_map, std::ostream &os) {
    JAST jobs(JSON_ARRAY);

    for (auto &job_pair: job_map) {
        const JobReflection &job = job_pair.second.job;
        JAST &job_json = jobs.add("", JSON_OBJECT);

        job_json.add("id", job.job);
        job_json.add("group", job_pair.second.is_on_critical_path ? 1 : 0);
        std::string label = job.label;
        job_json.add("content", (!(job.label.empty()) ? label : std::to_string(job.job)));
        job_json.add("start", job_pair.second.starttime);
        job_json.add("end", job_pair.second.endtime);

        std::stringstream title;
        title << job.job << "<br>"
              << (!(job.label.empty()) ? job.label + "<br>" : "")
              << "Command line:<br>";
        for (std::string line: job.commandline) {
            line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
            title << line << " ";
        }
        if (!(job.outputs.empty())) {
            title << "<br>Output file:";
            for (const auto &output: job.outputs) {
                title << "<br>" << output.path;
            }
        }
        job_json.add("title", title.str());
    }
    os << jobs;
}

void write_critical_arrows(const std::vector<long> &critical_path, std::ostream &os) {
    JAST arrows(JSON_ARRAY);
    for (size_t i = 0; i + 1 < critical_path.size(); i++) {
        JAST &arrow = arrows.add("", JSON_OBJECT);
        arrow.add("id", (int) i);
        arrow.add("id_item_1", critical_path[i]);
        arrow.add("id_item_2", critical_path[i + 1]);
    }
    os << arrows;
}

void write_all_arrows(const std::map<long, JobNode> &job_map, size_t critical_path_size, std::ostream &os) {
    JAST arrows(JSON_ARRAY);
    size_t id = critical_path_size;
    for (auto &job_pair: job_map) {
        JobNode job_node = job_pair.second;
        for (long dependency: job_node.dependencies) {
            JAST &arrow = arrows.add("", JSON_OBJECT);
            arrow.add("id", (int) id);
            arrow.add("id_item_1", job_node.job.job);
            arrow.add("id_item_2", dependency);
            id++;
        }
    }
    os << arrows;
}

void write_html(const std::map<long, JobNode> &job_map, const std::vector<long> &critical_path, std::ostream &os) {
    std::ifstream html_template(find_execpath() + "/../share/wake/html/timeline_template.html");
    std::ifstream arrow(find_execpath() + "/../share/wake/html/timeline_arrow.js");
    std::ifstream main(find_execpath() + "/../share/wake/html/timeline_main.js");
    os << html_template.rdbuf();

    os << R"(<script type="application/json" id="jobs">)" << std::endl;
    write_jobs(job_map, os);
    os << "</script>" << std::endl;
    os << R"(<script type="application/json" id="criticalPathArrows">)" << std::endl;
    write_critical_arrows(critical_path, os);
    os << "</script>" << std::endl;
    os << R"(<script type="application/json" id="allArrows">)" << std::endl;
    write_all_arrows(job_map, critical_path.size(), os);
    os << "</script>" << std::endl;

    os << R"(<script type="text/javascript">)" << std::endl;
    os << arrow.rdbuf();
    os << "</script>" << std::endl;

    os << R"(<script type="module">)" << std::endl;
    os << main.rdbuf();
    os << "</script>\n"
          "</body>\n"
          "</html>\n";
}

void create_timeline(Database &db) {
    std::vector<JobReflection> jobs = db.get_job_visualization();
    std::map<long, JobNode> job_map;
    for (JobReflection &job: jobs) {
        job_map.insert({job.job, JobNode(job)});
    }

    std::vector<FileAccess> accesses = db.get_file_accesses();
    fill_all_dependencies(accesses, job_map);

    std::vector<long> top_sorted_jobs = top_sort(job_map);
    assign_parents(job_map, top_sorted_jobs);
    transitive_reduction(job_map);

    std::vector<long> critical_path;
    find_critical_path(job_map, critical_path);

    write_html(job_map, critical_path, std::cout);
}
