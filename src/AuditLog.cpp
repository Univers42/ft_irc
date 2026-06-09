#include "AuditLog.hpp"

#include <ctime>
#include <vector>

AuditLog::AuditLog(const std::string &path)
	: _csv()
{
	/* CsvWriter probes the file before opening, so the header is written
	** exactly once per file lifetime. */
	if (_csv.open(path) && _csv.isNewFile())
	{
		std::vector<std::string> header;
		header.push_back("timestamp");
		header.push_back("event");
		header.push_back("actor");
		header.push_back("detail");
		_csv.row(header);
	}
}

AuditLog::~AuditLog()
{
	_csv.close();
}

/* ─── IServerExtension ─── */

const char *AuditLog::name() const
{
	return "audit-log";
}

void AuditLog::onAudit(const std::string &event, const std::string &actor,
					   const std::string &detail)
{
	log(event, actor, detail);
}

bool AuditLog::ok() const
{
	return _csv.ok();
}

std::string AuditLog::timestamp()
{
	std::time_t now = std::time(NULL);
	std::tm *lt = std::localtime(&now);
	char buf[32];
	std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%S", lt);
	return std::string(buf);
}

void AuditLog::log(const std::string &event, const std::string &actor,
				   const std::string &detail)
{
	if (!_csv.ok())
		return;
	std::vector<std::string> row;
	row.push_back(timestamp());
	row.push_back(event);
	row.push_back(actor);
	row.push_back(detail);
	_csv.row(row);
}
