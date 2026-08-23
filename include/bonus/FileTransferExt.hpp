#ifndef FILETRANSFEREXT_HPP
#define FILETRANSFEREXT_HPP

#include <map>
#include <string>

#include "Dispatch.hpp"
#include "ext/IServerExtension.hpp"

class FileTransferExt : public IServerExtension {
 public:
  FileTransferExt();
  ~FileTransferExt();

  const char* name() const;
  bool onCommand(Server& server, Client& client, const Message& msg);
  void onClientDisconnect(Server& server, Client& client, const std::string& reason);
  void onTick(Server& server, time_t now);

  static const unsigned long MAX_FILE_SIZE = 50UL * 1024UL * 1024UL;
  static const size_t MAX_CHUNK_B64 = 400;
  static const size_t MAX_FILENAME = 64;
  static const time_t IDLE_TIMEOUT = 60;

 private:
  FileTransferExt(const FileTransferExt&);
  FileTransferExt& operator=(const FileTransferExt&);

  struct Transfer {
    int senderFd;
    int recipientFd;
    std::string filename;
    unsigned long declaredSize;
    unsigned long relayedBytes;
    bool accepted;
    time_t lastActivity;
  };

  typedef void (FileTransferExt::*SubHandler)(Server& server, Client& client, const Message& msg);
  typedef Dispatch::Entry<SubHandler> SubCommand;

  static const SubCommand kSubCommands[];

  void cmdSend(Server& server, Client& client, const Message& msg);
  void cmdAnswer(Server& server, Client& client, const Message& msg, bool accept);
  void cmdAccept(Server& server, Client& client, const Message& msg);
  void cmdReject(Server& server, Client& client, const Message& msg);
  void cmdData(Server& server, Client& client, const Message& msg);
  void cmdEnd(Server& server, Client& client, const Message& msg);
  void cmdAbort(Server& server, Client& client, const Message& msg);

  void notice(Server& server, Client& client, const std::string& text);
  void abortTransfer(Server& server, long id, const std::string& why);
  Transfer* findById(long id);
  long findActive(int senderFd, int recipientFd) const;

  std::map<long, Transfer> _transfers;
  long _nextId;
};

#endif
