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

  /* ─── Getters ─── */
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
  /* The member list as one or more space-separated chunks, each at most
  ** `budget` bytes, so RPL_NAMREPLY can stay inside the 512-byte line
  ** limit on a channel of any size. Always returns at least one chunk. */
  std::vector<std::string> getNamesChunks(size_t budget) const;

  /* ─── Setters ─── */
  void setTopic(const std::string& topic, const std::string& setter);
  void setKey(const std::string& key);
  void removeKey();
  void setUserLimit(size_t limit);
  void removeUserLimit();
  void setInviteOnly(bool inviteOnly);
  void setTopicRestricted(bool restricted);
  /* ─── Member management ─── */
  
  /**
   * @brief Add a Client (user) to a Channel.
   * @param client a pointer to a Client instance. 
   * @note Channel has a map Channel._members, declared as: std::map<int, Client>.
   * The key: is the descriptor of a Client, a value: pointer to a Client instance.
   */

  void addMember(Client* client);
  
  /**
   * @brief remove member from a channel 
   * @param client - a pointer to a Client instance.
   * @note removes a Client from two sets Channel._operators and Channel._members using client.getFd() as input parameter.
   */
  void removeMember(Client* client);

  /** 
	@brief checks if a Client is a member of a channel.
	@return true if a Channel._members tiene mas de un elemento con id Client.getFd();
  */
  bool isMember(Client* client) const;

  /**
   * @brief checks if a channek has no members.
   */
  bool isEmpty() const;
  
  /* ─── Operator management ─── */

  /**
   * @brief if a Client is Operator of a channel.
   */
  bool isOperator(Client* client) const;

  /**
   * @brief makes or disables a client an operator of the channel.
   * @param client - Client instance.
   * @param op - if true, set a Client as operator of the channel.
   *           - if false, disable a Client as operator of the channel.
   * Add]s a client to a set Channel._operators ( std::set<int> _operators;) 
   *
   */
  void setOperator(Client* client, bool op);

  /* ─── Invite management ─── */
  /* Keyed by the invited *connection*, never by its nickname: a name can
  ** be released (NICK) or freed (QUIT) and picked up by someone else,
  ** which would hand them a +i channel they were never invited to. */
  void addInvite(Client* client);
  bool isInvited(Client* client) const;
  void removeInvite(Client* client);










  /* ─── Messaging ─── */
  void broadcastMessage(const std::string& msg, Client* exclude);










  /* ─── Utility ─── */
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
