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

#include "timeline.h"
#include "runtime/database.h"
#include "util/execpath.h"

struct JobNode {
    JobReflection &job;
    long parent_id = -1;
    std::vector<long> dependencies;

    long long starttime;
    long long endtime;

    long long hypothetical_starttime = 0;
    long long hypothetical_endtime = -1;

    bool top_sort_visited = false;
    bool is_on_critical_path = false;
    bool transitive_reduction_visited = false;

    explicit JobNode(JobReflection &_job) : job(_job) {
        std::string start_str = std::to_string(_job.starttime.asInt64());
        std::string end_str = std::to_string(_job.endtime.asInt64());
        starttime = std::stoll(start_str.substr(0, start_str.length() - 6));
        endtime = std::stoll(end_str.substr(0, end_str.length() - 6));
    }
};

void dfs_top_sort(long job_id, std::map<long, JobNode> &job_map, std::vector<long> &top_sorted_jobs) {
    JobNode &job = job_map.at(job_id);
    job_map.at(job_id).top_sort_visited = true;
    for (auto it = job.dependencies.begin(); it < job.dependencies.end(); it++) {
        if (job_map.find(*it) == job_map.end()) {
            job.dependencies.erase(it--);
            continue;
        }
        if (!(job_map.at(*it).top_sort_visited)) {
            dfs_top_sort(*it, job_map, top_sorted_jobs);
        }
    }
    top_sorted_jobs.emplace_back(job_id);
}

void top_sort(std::map<long, JobNode> &job_map, std::vector<long> &top_sorted_jobs) {
    for (const auto &job_pair: job_map) {
        if (!(job_pair.second.top_sort_visited)) {
            dfs_top_sort(job_pair.first, job_map, top_sorted_jobs);
        }
    }
}

long fill_one_dependency(std::pair<int, long> access, long dependency, std::map<long, JobNode> &job_map) {
    if (access.first == 2) {
        return access.second;
    }
    if (job_map.find(access.second) != job_map.end()) {
        job_map.at(access.second).dependencies.emplace_back(dependency);
    }
    return dependency;
}

void fill_all_dependencies(const std::vector<std::pair<int, long>> &accesses, std::map<long, JobNode> &job_map) {
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

void dfs_transitive_reduction(long job_id, std::map<long, JobNode> &job_map, std::vector<int> &ancestors) {
    JobNode &job = job_map.at(job_id);
    job_map.at(job_id).top_sort_visited = true;
    ancestors.emplace_back(job_id);

    for (long child_id: job.dependencies) {
        JobNode &child = job_map.at(child_id);
        for (long grandchild_id: child.dependencies) {
            for (int ancestor_id: ancestors) {
                JobNode ancestor = job_map.at(ancestor_id);
                if (std::find(ancestor.dependencies.begin(), ancestor.dependencies.end(), grandchild_id) !=
                    ancestor.dependencies.end()) {
                    std::remove(ancestor.dependencies.begin(), ancestor.dependencies.end(), grandchild_id);
                }
            }
        }
        dfs_transitive_reduction(child_id, job_map, ancestors);
    }
    ancestors.pop_back();
}

void transitive_reduction(std::map<long, JobNode> &job_map) {
    std::vector<int> ancestors;
    for (const auto &job_pair: job_map) {
        if (!(job_pair.second.transitive_reduction_visited)) {
            dfs_transitive_reduction(job_pair.first, job_map, ancestors);
        }
    }
}

void write_jobs(std::map<long, JobNode> &job_map, std::ostream &os) {
    os << "const jobs = new vis.DataSet([\n";
    for (const auto &job_pair: job_map) {
        JobReflection &job = job_pair.second.job;
        os << "        {id: " << job.job << ", group: " << (job_pair.second.is_on_critical_path ? 1 : 0)
           << ", content: '" << (!(job.label.empty()) ? job.label : std::to_string(job.job))
           << "', start: '" << job_pair.second.starttime
           << "', end: '" << job_pair.second.endtime
           << "', title: '" << job.job << "<br>"
           << (!(job.label.empty()) ? job.label + "<br>" : "")
           << "Command line:<br>";
        for (std::string line: job.commandline) {
            line.erase(std::remove(line.begin(), line.end(), '\n'), line.end());
            os << line << " ";
        }
        if (!(job.outputs.empty())) {
            os << "<br>Output file:";
            for (const auto &output: job.outputs) {
                os << "<br>" << output.path;
            }
        }
        os << "'},\n";
    }
    os << "]);\n\n";
}

void write_critical_arrows(std::vector<long> &critical_path, std::ostream &os) {
    os << "const critical_path_arrows = [\n";
    for (size_t i = 0; i < critical_path.size() - 1; i++) {
        os << "{ id: " << i << ", id_item_1: " << critical_path[i] << ", id_item_2: " << critical_path[i + 1]
                  << " },\n";
    }
    os << "];\n\n";
}

void write_all_arrows(std::map<long, JobNode> &job_map, size_t critical_path_size, std::ostream &os) {
    os << "const all_arrows = [\n";
    size_t id = critical_path_size;
    for (const auto &job_pair: job_map) {
        JobNode job_node = job_pair.second;
        for (long dependency: job_node.dependencies) {
            os << "{ id: " << id << ", id_item_1: " << job_node.job.job << ", id_item_2: " << dependency
                      << " },\n";
            id++;
        }
    }
    os << "];\n\n";
}

void write_html(std::ostream &os, std::map<long, JobNode> &job_map, std::vector<long> &critical_path) {
    std::ifstream html_template(find_execpath() + "/../share/wake/html/timeline_template.html");
    std::ifstream arrow(find_execpath() + "/../share/wake/html/timeline_arrow.js");
    std::ifstream main(find_execpath() + "/../share/wake/html/timeline_main.js");
    os << html_template.rdbuf();

    os << "<script type=\"text/javascript\">" << std::endl;
    write_jobs(job_map, os);
    write_critical_arrows(critical_path, os);
    write_all_arrows(job_map, critical_path.size(), os);
    os << "</script>" << std::endl;

    os << "<script type=\"text/javascript\">" << std::endl;
    os << arrow.rdbuf();
    os << "</script>" << std::endl;

    os << "<script type=\"module\">" << std::endl;
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

    std::vector<std::pair<int, long>> accesses = db.get_file_accesses();
    fill_all_dependencies(accesses, job_map);

    std::vector<long> top_sorted_jobs;
    top_sort(job_map, top_sorted_jobs);
    assign_parents(job_map, top_sorted_jobs);
    transitive_reduction(job_map);

    std::vector<long> critical_path;
    find_critical_path(job_map, critical_path);

    write_html(std::cout, job_map, critical_path);
}
