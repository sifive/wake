#include <location.h>

enum Severity { S_ERROR = 1, S_WARNING, S_INFORMATION, S_HINT };

class Diagnostic {
  public:
    Diagnostic(Location l_, Severity severity_, std::string message_)
      : l(l_), severity(severity_), message(message_) { }

    Location getLocation() const { return l; }
    Severity getSeverity() const { return severity; }
    std::string getMessage() const { return message; }    

  private:
    const Location l;
    const Severity severity;
    const std::string message;      
};