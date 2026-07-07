/**
 * ConsoleReporter.h — structured serial console for commissioning & diagnostics.
 *
 * Gives an industrial controller a readable, self-describing serial output:
 *   - a boxed startup banner,
 *   - leveled log lines (INFO / WARN / ERROR) with an uptime stamp,
 *   - section headers and aligned "label ....... value" rows for status tables.
 *
 * ANSI colors are optional (some field terminals / gateways choke on them), so
 * they can be turned off at begin(). Reusable on any Arduino Stream.
 */
#ifndef INDUSTRIALCORE_CONSOLEREPORTER_H
#define INDUSTRIALCORE_CONSOLEREPORTER_H

#include <Arduino.h>

class ConsoleReporter {
public:
  enum Level { INFO, WARN, ERROR };

  void begin(Stream &io, bool ansi = true);

  // Boxed title banner, optionally with a subtitle line.
  void banner(const char *title, const char *subtitle = nullptr);

  // Leveled log line: "[   12.345] INFO  message".
  void log(Level level, const char *fmt, ...) __attribute__((format(printf, 3, 4)));

  // Status-table helpers.
  void section(const char *title);                    // "── TITLE ──────────"
  void row(const char *label, const char *fmt, ...) __attribute__((format(printf, 3, 4)));
  void rule();                                        // horizontal separator
  void blank();                                       // empty line

private:
  Stream *_io   = nullptr;
  bool    _ansi = true;
  void    ansi(const char *code);
  void    stamp();
};

#endif /* INDUSTRIALCORE_CONSOLEREPORTER_H */
