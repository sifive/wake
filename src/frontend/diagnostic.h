#ifndef DIAGNOSTIC_H
#define DIAGNOSTIC_H

#include <location.h>
#include <iostream>

enum Severity { S_ERROR = 1, S_WARNING, S_INFORMATION, S_HINT };

class Diagnostic {
  public:
    Diagnostic(Location location_, Severity severity_, std::string message_)
      : location(location_), severity(severity_), message(message_) { }

    Location getLocation() const { return location; }
    Severity getSeverity() const { return severity; }
    std::string getMessage() const { return message; }  

  private:
    const Location location;
    const Severity severity;
    const std::string message;   
};

class DiagnosticReporter {
  public:
    virtual void report(Diagnostic diagnostic) = 0;
    void report(Location location, Severity severity, std::string message) {
      report(Diagnostic(location, severity, std::move(message)));
    }
};

#endif
