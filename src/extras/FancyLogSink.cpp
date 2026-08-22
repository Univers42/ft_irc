#include "extras/FancyLogSink.hpp"

#include <ctime>
#include <iostream>
#include <string>

#include "libcpp/str/format.hpp"
#include "libcpp/term/color.hpp"
#include "libcpp/term/style.hpp"
#include "libcpp/term/writer.hpp"

/*
** Callout kinds (banner/info/warn/…) spin up a fresh TermWriter per call and
** emit one styled element. Those fire on connection-level events, so the cost
** is irrelevant and no shared mutable state is needed.
**
** The protocol trace does NOT go through TermWriter: it fires once per IRC
** line, which at trace level is the hottest path in the process, and a
** TermWriter callout would box every line in a frame. It composes ANSI
** directly from libcpp's Srgb instead — the same colour type the rest of the
** styling uses, so the palette stays consistent.
*/

namespace {

/* One palette, defined once. Chosen for legibility on both dark and light
** terminals: the two directions are complementary rather than merely
** different brightnesses, so they stay distinguishable when a terminal
** remaps the 16 base colours. */
libcpp::Srgb dirIn() { return libcpp::Srgb(90, 200, 250); }    /* client -> server */
libcpp::Srgb dirOut() { return libcpp::Srgb(160, 230, 130); }  /* server -> client */
libcpp::Srgb dimGrey() { return libcpp::Srgb(120, 120, 130); }
libcpp::Srgb peerCol() { return libcpp::Srgb(230, 200, 120); }
libcpp::Srgb cmdCol() { return libcpp::Srgb(235, 235, 245); }
libcpp::Srgb errCol() { return libcpp::Srgb(240, 120, 120); }
libcpp::Srgb noteCol() { return libcpp::Srgb(150, 130, 200); }

const char* kReset = "\033[0m";

std::string paint(const libcpp::Srgb& c, const std::string& text) {
  return c.to_ansi_fg() + text + kReset;
}

std::string stamp() {
  std::time_t now = std::time(NULL);
  std::tm* lt = std::localtime(&now);
  char buf[16];
  if (!lt || std::strftime(buf, sizeof(buf), "%H:%M:%S", lt) == 0)
    return "--:--:--";
  return std::string(buf);
}

/* An IRC error numeric is 4xx/5xx/6xx; a reply is 0xx/2xx/3xx. Colouring the
** command by that distinction is what makes a failing session visible by
** scrolling rather than by reading. */
bool isErrorNumeric(const std::string& note) {
  if (note.size() < 3) return false;
  char c = note[0];
  return (c == '4' || c == '5' || c == '6') &&
         note[1] >= '0' && note[1] <= '9' && note[2] >= '0' && note[2] <= '9';
}

/* Split an IRC line into the part before the trailing parameter and the
** trailing itself, so the payload can be dimmed relative to the framing.
** Returns npos in `at` when there is no trailing. */
void splitTrailing(const std::string& line, std::string& head,
                   std::string& trail) {
  /* The trailing parameter opens at the first " :" that is not inside the
  ** prefix -- so start the search after a leading ":prefix" if present. */
  std::string::size_type from = 0;
  if (!line.empty() && line[0] == ':') {
    std::string::size_type sp = line.find(' ');
    if (sp == std::string::npos) {
      head = line;
      trail = "";
      return;
    }
    from = sp;
  }
  std::string::size_type at = line.find(" :", from);
  if (at == std::string::npos) {
    head = line;
    trail = "";
    return;
  }
  head = line.substr(0, at + 2);
  trail = line.substr(at + 2);
}

}  // namespace

FancyLogSink::FancyLogSink() {}

FancyLogSink::~FancyLogSink() {}

void FancyLogSink::write(char kind, const std::string& msg) {
  std::ostream& os = (kind == 'w' || kind == 'e') ? std::cerr : std::cout;

  /* Debug and trace bypass TermWriter deliberately: they are line-rate and a
  ** boxed callout per line would be unreadable. They were previously absent
  ** from this switch entirely, which silently swallowed every one of them. */
  if (kind == 'd' || kind == 't') {
    os << paint(dimGrey(), stamp()) << "  " << msg << std::endl;
    return;
  }

  libcpp::TermStyle ts;
  libcpp::TermWriter w(ts, os);
  switch (kind) {
    case 'b':
      w.h1(msg);
      break;
    case 'i':
      w.info(msg);
      break;
    case 's':
      w.success(msg);
      break;
    case 'w':
      w.warn(msg);
      break;
    case 'e':
      w.error(msg);
      break;
  }
  w.flush();
}

void FancyLogSink::protocol(char dir, int fd, const std::string& peer,
                            const std::string& line, const std::string& note) {
  bool inbound = (dir == '<');
  libcpp::Srgb arrowCol = inbound ? dirIn() : dirOut();
  std::string arrow = inbound ? "<<" : ">>";

  std::string head;
  std::string trail;
  splitTrailing(line, head, trail);

  /* The command word is what the eye looks for, so it is the only part shown
  ** at full brightness -- red when the line carries an error numeric. */
  libcpp::Srgb bodyCol = isErrorNumeric(note) ? errCol() : cmdCol();

  std::string out;
  out += paint(dimGrey(), stamp());
  out += "  ";
  out += paint(dimGrey(),
               "fd " + libcpp::str::pad_left(libcpp::str::to_string(fd), 3, ' '));
  out += "  ";
  out += paint(arrowCol, arrow);
  out += "  ";
  out += paint(peerCol(), libcpp::str::pad_right(peer.empty() ? "*" : peer, 9, ' '));
  out += "  ";
  out += paint(bodyCol, head);
  if (!trail.empty()) out += paint(dimGrey(), trail);
  if (!note.empty()) out += "  " + paint(noteCol(), "[" + note + "]");

  std::cout << out << std::endl;
}
