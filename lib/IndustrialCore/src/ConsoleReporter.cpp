#include "ConsoleReporter.h"
#include <stdarg.h>
#include <string.h>

// ANSI color codes (only emitted when _ansi is true).
static const char *C_RESET = "\033[0m";
static const char *C_DIM   = "\033[2m";
static const char *C_CYAN  = "\033[36m";
static const char *C_GREEN = "\033[32m";
static const char *C_YELLOW= "\033[33m";
static const char *C_RED   = "\033[31m";
static const char *C_BOLD  = "\033[1m";

static const uint8_t LABEL_WIDTH = 22;

void ConsoleReporter::begin(Stream &io, bool ansi) {
  _io   = &io;
  _ansi = ansi;
}

void ConsoleReporter::ansi(const char *code) {
  if (_ansi && _io) _io->print(code);
}

void ConsoleReporter::stamp() {
  // Uptime in seconds.milliseconds, right-aligned to 10 chars.
  uint32_t ms = millis();
  char buf[16];
  snprintf(buf, sizeof(buf), "[%6lu.%03lu] ",
           (unsigned long)(ms / 1000), (unsigned long)(ms % 1000));
  ansi(C_DIM);
  _io->print(buf);
  ansi(C_RESET);
}

void ConsoleReporter::banner(const char *title, const char *subtitle) {
  if (!_io) return;
  const char *bar =
    "==============================================================";
  _io->println();
  ansi(C_CYAN); ansi(C_BOLD);
  _io->println(bar);
  _io->print("  "); _io->println(title);
  if (subtitle) { ansi(C_RESET); ansi(C_DIM); _io->print("  "); _io->println(subtitle); ansi(C_CYAN); }
  ansi(C_BOLD);
  _io->println(bar);
  ansi(C_RESET);
}

void ConsoleReporter::log(Level level, const char *fmt, ...) {
  if (!_io) return;
  stamp();
  const char *tag; const char *color;
  switch (level) {
    case WARN:  tag = "WARN "; color = C_YELLOW; break;
    case ERROR: tag = "ERROR"; color = C_RED;    break;
    default:    tag = "INFO "; color = C_GREEN;  break;
  }
  ansi(color); _io->print(tag); _io->print(' '); ansi(C_RESET);

  char buf[160];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  _io->println(buf);
}

void ConsoleReporter::section(const char *title) {
  if (!_io) return;
  ansi(C_CYAN);
  _io->print("-- "); _io->print(title); _io->print(' ');
  int pad = 40 - (int)strlen(title);
  for (int i = 0; i < pad; i++) _io->print('-');
  _io->println();
  ansi(C_RESET);
}

void ConsoleReporter::row(const char *label, const char *fmt, ...) {
  if (!_io) return;
  _io->print("  ");
  _io->print(label);
  int pad = (int)LABEL_WIDTH - (int)strlen(label);
  ansi(C_DIM);
  for (int i = 0; i < pad; i++) _io->print('.');
  ansi(C_RESET);
  _io->print(' ');

  char buf[128];
  va_list ap; va_start(ap, fmt);
  vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  _io->println(buf);
}

void ConsoleReporter::rule() {
  if (!_io) return;
  ansi(C_DIM);
  _io->println("  ----------------------------------------------------");
  ansi(C_RESET);
}

void ConsoleReporter::blank() {
  if (_io) _io->println();
}
