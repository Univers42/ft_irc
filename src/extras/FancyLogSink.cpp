#include "extras/FancyLogSink.hpp"

#include <iostream>
#include <string>

#include "libcpp/data/date.hpp"
#include "libcpp/str/format.hpp"
#include "libcpp/term/color.hpp"
#include "libcpp/term/style.hpp"
#include "libcpp/term/writer.hpp"

namespace {
libcpp::Srgb dirIn() { return libcpp::Srgb(90, 200, 250); }
libcpp::Srgb dirOut() { return libcpp::Srgb(160, 230, 130); }
libcpp::Srgb dimGrey() { return libcpp::Srgb(120, 120, 130); }
libcpp::Srgb peerCol() { return libcpp::Srgb(230, 200, 120); }
libcpp::Srgb cmdCol() { return libcpp::Srgb(235, 235, 245); }
libcpp::Srgb errCol() { return libcpp::Srgb(240, 120, 120); }
libcpp::Srgb noteCol() { return libcpp::Srgb(150, 130, 200); }

std::string paint(const libcpp::Srgb& c, const std::string& text) {
  return libcpp::TermUtils::apply_fg(c) + text + libcpp::TermUtils::reset();
}

bool isErrorNumeric(const std::string& note) {
  if (note.size() < 3) return false;
  char c = note[0];
  return (c == '4' || c == '5' || c == '6') && note[1] >= '0' && note[1] <= '9' && note[2] >= '0' && note[2] <= '9';
}

void splitTrailing(const std::string& line, std::string& head, std::string& trail) {
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

  if (kind == 'd' || kind == 't') {
    os << paint(dimGrey(), libcpp::data::time_hms()) << "  " << msg << std::endl;
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

void FancyLogSink::protocol(char dir, int fd, const std::string& peer, const std::string& line,
                            const std::string& note) {
  bool inbound = (dir == '<');
  libcpp::Srgb arrowCol = inbound ? dirIn() : dirOut();
  std::string arrow = inbound ? "<<" : ">>";

  std::string head;
  std::string trail;
  splitTrailing(line, head, trail);

  libcpp::Srgb bodyCol = isErrorNumeric(note) ? errCol() : cmdCol();

  std::string out;
  out += paint(dimGrey(), libcpp::data::time_hms());
  out += "  ";
  out += paint(dimGrey(), Log::fdField(fd));
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
