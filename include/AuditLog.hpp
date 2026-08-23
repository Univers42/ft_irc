#ifndef AUDITLOG_HPP
#define AUDITLOG_HPP

#include <string>

#include "ext/IServerExtension.hpp"
#include "libcpp98/csv_writer.hpp"

class AuditLog : public IServerExtension {
 public:
  explicit AuditLog(const std::string& path);
  ~AuditLog();

  const char* name() const;
  void onAudit(const std::string& event, const std::string& actor, const std::string& detail);
  bool ok() const;
  void log(const std::string& event, const std::string& actor, const std::string& detail);

 private:
  AuditLog();
  AuditLog(const AuditLog& other);
  AuditLog& operator=(const AuditLog& other);
  static std::string timestamp();
  libcpp98::CsvWriter _csv;
};

#endif
