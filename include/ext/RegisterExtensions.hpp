#ifndef REGISTEREXTENSIONS_HPP
#define REGISTEREXTENSIONS_HPP

class Server;

/* Both the bonus and the full tier compose the same two bonus extensions and
** report the same failure, in their own registerExtensions(). The composition
** is deliberately per-tier -- that is what a tier is -- but the wording of the
** failure is one fact, so it lives here rather than in each of them.
**
** Defined in the header, not declared extern: a namespace-scope const has
** internal linkage in C++98, so every tier gets its own copy and no TU has to
** own the definition -- the same shape Replies.hpp uses for the reply codes. */
const char* const kBonusExtensionsFailed = "could not create bonus extensions (out of memory)";

void configureSettings();
void registerExtensions(Server& server);

#endif
