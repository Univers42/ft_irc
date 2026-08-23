#ifndef CHANNELMODES_HPP
#define CHANNELMODES_HPP

#include <cstddef>
#include <string>

namespace ChannelModes {

struct Spec {
  char letter;
  bool paramOnAdd;
  bool paramOnRemove;
  bool spareParamOnRemove;
};

inline const Spec* table() {
  static const Spec kSpecs[] = {
      {'i', false, false, false}, {'t', false, false, false}, {'k', true, false, true},
      {'o', true, true, false},   {'l', true, false, false},  {'\0', false, false, false},
  };
  return kSpecs;
}

inline const Spec* find(char letter) {
  for (const Spec* spec = table(); spec->letter != '\0'; ++spec)
    if (spec->letter == letter) return spec;
  return 0;
}

inline bool takesParam(const Spec& spec, bool adding) { return adding ? spec.paramOnAdd : spec.paramOnRemove; }

inline std::size_t mandatoryParams(const std::string& modeStr, std::size_t from, bool sign) {
  std::size_t need = 0;
  bool adding = sign;
  for (std::size_t i = from; i < modeStr.size(); ++i) {
    const char c = modeStr[i];
    if (c == '+') {
      adding = true;
      continue;
    }
    if (c == '-') {
      adding = false;
      continue;
    }
    const Spec* spec = find(c);
    if (spec != 0 && takesParam(*spec, adding)) ++need;
  }
  return need;
}

inline bool firstKeyParam(const std::string& modeStr, std::size_t available, std::size_t* out) {
  bool adding = true;
  std::size_t used = 0;
  for (std::size_t i = 0; i < modeStr.size(); ++i) {
    const char c = modeStr[i];
    if (c == '+') {
      adding = true;
      continue;
    }
    if (c == '-') {
      adding = false;
      continue;
    }
    const Spec* spec = find(c);
    if (spec == 0) continue;

    if (adding && c == 'k') {
      if (used >= available) return false;
      *out = used;
      return true;
    }
    if (takesParam(*spec, adding)) {
      if (used < available) ++used;
      continue;
    }
    if (!adding && spec->spareParamOnRemove) {
      const std::size_t stillNeeded = mandatoryParams(modeStr, i + 1, adding);
      if (used < available && available - used > stillNeeded) ++used;
    }
  }
  return false;
}

}  // namespace ChannelModes

#endif
