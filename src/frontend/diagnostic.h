/*
 * Copyright 2019 SiFive, Inc.
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

#ifndef DIAGNOSTIC_H
#define DIAGNOSTIC_H

#include <sstream>

#include "location.h"

enum Severity { S_ERROR = 1, S_WARNING, S_INFORMATION, S_HINT };

class Diagnostic {
  public:
    Diagnostic(Location location_, Severity severity_, std::string message_)
      : location(location_), severity(severity_), message(message_) { }

    Location getLocation() const { return location; }
    Severity getSeverity() const { return severity; }
    std::string getMessage() const { return message; }
    std::string getFilename() const { return getLocation().filename; }

  private:
    const Location location;
    const Severity severity;
    const std::string message;
};

class DiagnosticReporter {
  public:
    void reportError(Location location, std::string message) {
      report(location, S_ERROR, message);
    }
    void reportWarning(Location location, std::string message) {
      report(location, S_WARNING, message);
    }
    void reportInfo(Location location, std::string message) {
      report(location, S_INFORMATION, message);
    }
    void reportHint(Location location, std::string message) {
      report(location, S_HINT, message);
    }

  private: 
    void report(Location location, Severity severity, std::string message) {
      report(Diagnostic(location, severity, std::move(message)));
    }

    virtual void report(Diagnostic diagnostic) = 0;    
};

extern DiagnosticReporter *reporter;

#define ERROR(loc, stream)                   \
  do {                                       \
    std::stringstream sstr;                  \
    sstr << stream;                          \
    reporter->reportError(loc, sstr.str());  \
  } while (0)                                \

#endif
