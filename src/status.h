#ifndef STATUS_H
#define STATUS_H

struct Status {
  double budget;
  bool stdout, stderr;
//  std::string cmdline;
};

extern bool refresh_needed;

void status_init();
//void status_set(std::vector<Status> &state);
void status_write(int fd, const char *data, int len);
void status_refresh();
void status_finish();

#endif
