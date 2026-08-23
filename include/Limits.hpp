#ifndef LIMITS_HPP
#define LIMITS_HPP

#include <cstddef>
#include <ctime>

namespace Limits {

const char* const kServerName = "ft_irc";
const char* const kServerVersion = "1.0";
const char* const kServerCreated = "2025-01-01";

const std::size_t kNickLen = 9;
const std::size_t kUserLen = 10;
const std::size_t kChannelLen = 50;
const std::size_t kTopicLen = 390;
const std::size_t kMsgLen = 512;
const std::size_t kKeyLen = 23;
const std::size_t kSendQ = 64 * 1024;
const std::size_t kMaxClients = 1024;
const long kUserLimit = 65535;

const time_t kPingInterval = 120;
const time_t kPingTimeout = 120;
const time_t kPingSweepInterval = 30;
const time_t kPendingCloseTimeout = 5;

}  // namespace Limits

#endif
