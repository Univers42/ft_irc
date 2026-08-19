#ifndef AUDITLOG_HPP
#define AUDITLOG_HPP

#include <string>

#include "ext/IServerExtension.hpp"
#include "libcpp98/csv_writer.hpp"

/**
* @class
* AuditLog — append-only CSV trail of server activity (connections, joins,
* disconnects, published events). Useful for the platform's compliance/history
* view. Config-gated: only created when [audit] enabled = true, so the plain
* RFC server writes nothing to disk.
*
* Plugged in through the extension seam: Server::audit() fans out to
* onAudit, which appends one CSV row.
*
* Columns: timestamp,event,actor,detail  (RFC-3339-ish local time)
*/

class AuditLog : public IServerExtension {
 public:

  explicit AuditLog(const std::string& path);
  ~AuditLog();

  /* ─── IServerExtension ─── */

  const char* name() const;
  void onAudit(const std::string& event, const std::string& actor,
               const std::string& detail);
  bool ok() const;
  
  /**
  * @brief The specific role of log is to print the detail of tehe event 
  * that an actor do speicfically, for isntance during a conversation with two users.
  * The log can be output to showcase all the conversation throughout all the iterations
  * with all teh details in history.
  * @param event a string const reference that give the name of the event
  * @param actor a string const 
  * @param detail
  * @return void
  * @note
  * Those deteail sare all buffered in a vector for fastness because the iterator faster than 
  */
  void log(const std::string& event, const std::string& actor,
           const std::string& detail);

 private:
  AuditLog();
  AuditLog(const AuditLog& other);
  AuditLog& operator=(const AuditLog& other);

  static std::string timestamp();

  libcpp98::CsvWriter _csv;
};

#endif /* AUDITLOG_HPP */
