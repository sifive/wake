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

#include "timeline.h"

#include <fstream>
#include <iostream>
#include <sstream>

#include "json/json5.h"
#include "runtime/database.h"
#include "util/execpath.h"

void write_job_reflections(std::ostream &os, const std::vector<JobReflection> &jobs) {
  JAST jobs_json(JSON_ARRAY);
  for (const JobReflection &jobReflection : jobs) {
    JAST &job_json = jobs_json.add("", JSON_OBJECT);
    job_json.add("job", jobReflection.job);
    job_json.add("label", jobReflection.label.c_str());
    job_json.add("stale", jobReflection.stale);
    job_json.add("directory", jobReflection.directory.c_str());

    std::stringstream commandline;
    for (const std::string &line : jobReflection.commandline) {
      commandline << line << " ";
    }
    job_json.add("commandline", commandline.str());

    std::stringstream environment;
    for (const std::string &line : jobReflection.environment) {
      environment << line << " ";
    }
    job_json.add("environment", environment.str());

    job_json.add("stack", jobReflection.stack.c_str());

    job_json.add("stdin_file", jobReflection.stdin_file.c_str());

    job_json.add("starttime", jobReflection.starttime.as_int64());
    job_json.add("endtime", jobReflection.endtime.as_int64());
    job_json.add("wake_start", jobReflection.wake_start.as_int64());

    job_json.add("wake_cmdline", jobReflection.wake_cmdline.c_str());

    // TODO: figure out what this is supposed to be
    // job_json.add("stdout_payload", jobReflection.stdout_payload.c_str());
    // job_json.add("stderr_payload", jobReflection.stderr_payload.c_str());

    std::stringstream usage;
    usage << "status: " << jobReflection.usage.status << "<br>"
          << "runtime: " << jobReflection.usage.runtime << "<br>"
          << "cputime: " << jobReflection.usage.cputime << "<br>"
          << "membytes: " << std::to_string(jobReflection.usage.membytes) << "<br>"
          << "ibytes: " << std::to_string(jobReflection.usage.ibytes) << "<br>"
          << "obytes: " << std::to_string(jobReflection.usage.obytes);
    job_json.add("usage", usage.str());

    std::stringstream visible;
    for (const auto &visible_file : jobReflection.visible) {
      visible << visible_file.path << "<br>";
    }
    job_json.add("visible", visible.str());

    std::stringstream inputs;
    for (const auto &input : jobReflection.inputs) {
      inputs << input.path << "<br>";
    }
    job_json.add("inputs", inputs.str());

    std::stringstream outputs;
    for (const auto &output : jobReflection.outputs) {
      outputs << output.path << "<br>";
    }
    job_json.add("outputs", outputs.str());

    std::stringstream tags;
    for (const auto &tag : jobReflection.tags) {
      tags << "{<br>"
           << "  job: " << tag.job << ",<br>"
           << "  uri: " << tag.uri << ",<br>"
           << "  content: " << tag.content << "<br>},<br>";
    }
    job_json.add("tags", tags.str());
  }
  os << jobs_json;
}

void write_file_accesses(std::ostream &os, const std::vector<FileAccess> &accesses) {
  JAST accesses_json(JSON_ARRAY);
  for (const FileAccess &access : accesses) {
    JAST &access_json = accesses_json.add("", JSON_OBJECT);
    access_json.add("type", access.type);
    access_json.add("job", access.job);
  }
  os << accesses_json;
}

void write_timeline(std::ostream &os, const std::vector<JobReflection> &jobs,
                    const std::vector<FileAccess> &accesses) {
  std::ifstream html_template(find_execpath() + "/../share/wake/html/timeline_template.html");
  std::ifstream arrow_library(find_execpath() + "/../share/wake/html/timeline_arrow_lib.js");
  std::ifstream main(find_execpath() + "/../share/wake/html/timeline_main.js");

  os << html_template.rdbuf();

  os << R"(<script type="application/json" id="jobReflections">)" << std::endl;
  write_job_reflections(os, jobs);
  os << "</script>" << std::endl;

  os << R"(<script type="application/json" id="fileAccesses">)" << std::endl;
  write_file_accesses(os, accesses);
  os << "</script>" << std::endl;

  os << R"(<script type="text/javascript">)" << std::endl;
  os << arrow_library.rdbuf();
  os << "</script>" << std::endl;

  os << R"(<script type="module">)" << std::endl;
  os << main.rdbuf();
  os << "</script>\n"
        "</body>\n"
        "</html>\n";
}

void get_and_write_job_reflections(std::ostream &os, const Database &db) {
  std::vector<JobReflection> jobs = db.get_job_visualization();
  write_job_reflections(os, jobs);
}

void get_and_write_file_accesses(std::ostream &os, const Database &db) {
  std::vector<FileAccess> accesses = db.get_file_accesses();
  write_file_accesses(os, accesses);
}

void get_and_write_timeline(std::ostream &os, const Database &db) {
  std::vector<JobReflection> jobs = db.get_job_visualization();
  std::vector<FileAccess> accesses = db.get_file_accesses();
  write_timeline(os, jobs, accesses);
}
