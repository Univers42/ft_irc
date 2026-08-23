#ifndef DISPATCH_HPP
#define DISPATCH_HPP

#include <string>

namespace Dispatch {
template <class Handler>
struct Entry {
  const char* name;
  Handler handler;
};

template <class T>
const T* find(const T* table, const std::string& name) {
  for (const T* entry = table; entry->name != 0; ++entry)
    if (name == entry->name) return entry;
  return 0;
}

}  // namespace Dispatch

#endif
