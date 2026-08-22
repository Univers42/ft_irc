#ifndef CHANNEL_HPP
#define CHANNEL_HPP

#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

class Client;

class Channel {
 public:
  Channel(const std::string& name, Client* creator);
  ~Channel();

  const std::string& getName() const;
  const std::string& getTopic() const;
  const std::string& getTopicSetter() const;
  time_t getTopicTime() const;
  time_t getCreationTime() const;
  const std::string& getKey() const;
  size_t getUserLimit() const;
  bool isInviteOnly() const;
  bool isTopicRestricted() const;
  size_t getMemberCount() const;
  std::string getModeString() const;
  std::string getModeParams() const;

  std::vector<std::string> getNamesChunks(size_t budget) const;

  void setTopic(const std::string& topic, const std::string& setter);
  void setKey(const std::string& key);
  void removeKey();
  void setUserLimit(size_t limit);
  void removeUserLimit();
  void setInviteOnly(bool inviteOnly);
  void setTopicRestricted(bool restricted);

  void addMember(Client* client);
  void removeMember(Client* client);
  bool isMember(Client* client) const;
  bool isEmpty() const;

  bool isOperator(Client* client) const;
  void setOperator(Client* client, bool op);

  void addInvite(Client* client);
  bool isInvited(Client* client) const;
  void removeInvite(Client* client);

  void broadcastMessage(const std::string& msg, Client* exclude);

  Client* findMember(const std::string& nickname) const;
  std::vector<Client*> getMembers() const;

 private:
  Channel();
  Channel(const Channel& other);
  Channel& operator=(const Channel& other);

  std::string _name;
  std::string _topic;
  std::string _topicSetter;
  time_t _topicTime;
  time_t _creationTime;
  std::string _key;
  size_t _userLimit;
  bool _inviteOnly;
  bool _topicRestricted;

  std::map<int, Client*> _members;
  std::set<int> _operators;
  std::set<int> _inviteList;
};

#endif
