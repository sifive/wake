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
#include <fstream>
#include <sstream>
#include <set>

#include "timeline.h"
#include "runtime/database.h"
#include "util/execpath.h"
#include "json/json5.h"

std::string create_job_reflections(const std::vector<JobReflection> &jobs) {
    JAST jobs_json(JSON_ARRAY);
    for (const JobReflection &jobReflection: jobs) {
        JAST &job_json = jobs_json.add("", JSON_OBJECT);
        job_json.add("job", jobReflection.job);
        job_json.add("label", jobReflection.label.c_str());
        job_json.add("stale", jobReflection.stale);
        job_json.add("directory", jobReflection.directory.c_str());

        std::stringstream commandline;
        for (const std::string &line: jobReflection.commandline) {
            commandline << line << " ";
        }
        job_json.add("commandline", commandline.str());

        std::stringstream environment;
        for (const std::string &line: jobReflection.environment) {
            environment << line << " ";
        }
        job_json.add("environment", environment.str());

        job_json.add("stack", jobReflection.stack.c_str());

        job_json.add("stdin_file", jobReflection.stdin_file.c_str());

        job_json.add("starttime", jobReflection.starttime.as_int64());
        job_json.add("endtime", jobReflection.endtime.as_int64());
        job_json.add("wake_start", jobReflection.wake_start.as_int64());

        job_json.add("wake_cmdline", jobReflection.wake_cmdline.c_str());
        job_json.add("stdout_payload", jobReflection.stdout_payload.c_str());
        job_json.add("stderr_payload", jobReflection.stderr_payload.c_str());

        std::stringstream usage;
        usage << "status: " << jobReflection.usage.status << "<br>" <<
              "runtime: " << jobReflection.usage.runtime << "<br>" <<
              "cputime: " << jobReflection.usage.cputime << "<br>" <<
              "membytes: " << std::to_string(jobReflection.usage.membytes) << "<br>" <<
              "ibytes: " << std::to_string(jobReflection.usage.ibytes) << "<br>" <<
              "obytes: " << std::to_string(jobReflection.usage.obytes);
        job_json.add("usage", usage.str());

        std::stringstream visible;
        for (const auto &visible_file: jobReflection.visible) {
            visible << visible_file.path << "<br>";
        }
        job_json.add("visible", visible.str());

        std::stringstream inputs;
        for (const auto &input: jobReflection.inputs) {
            inputs << input.path << "<br>";
        }
        job_json.add("inputs", inputs.str());

        std::stringstream outputs;
        for (const auto &output: jobReflection.outputs) {
            outputs << output.path << "<br>";
        }
        job_json.add("outputs", outputs.str());

        std::stringstream tags;
        for (const auto &tag: jobReflection.tags) {
            tags << tag.content << ",<br><br>";
        }
        job_json.add("tags", tags.str());
    }
    std::stringstream buffer;
    buffer << jobs_json;
    return buffer.str();
}

std::string create_file_accesses(std::vector<FileAccess> &accesses) {
    JAST accesses_json(JSON_ARRAY);
    for (const FileAccess &access: accesses) {
        JAST &access_json = accesses_json.add("", JSON_OBJECT);
        access_json.add("type", access.type);
        access_json.add("job", access.job);
    }
    std::stringstream buffer;
    buffer << accesses_json;
    return buffer.str();
}

void write_html(std::vector<JobReflection> &jobs, std::vector<FileAccess> &accesses, std::ostream &os) {
    std::ifstream html_template(find_execpath() + "/../share/wake/html/timeline.html");
    std::stringstream buffer;
    buffer << html_template.rdbuf();
    std::string output = buffer.str();
    size_t pos = output.find("{0}");
    output.replace(pos, 3, create_job_reflections(jobs));
    pos = output.find("{1}");
    output.replace(pos, 3, create_file_accesses(accesses));
    os << output;
}

void create_timeline(Database &db) {
    std::vector<JobReflection> jobs = db.get_job_visualization();
    std::vector<FileAccess> accesses = db.get_file_accesses();
    write_html(jobs, accesses, std::cout);
}
