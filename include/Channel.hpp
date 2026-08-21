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
  /**
   * @brief returns a number of Clients(users) in a Channel(chat).
   * It uses _members.size() std::map method.
   * _members is a  std::map<int, Client>.
   */
  size_t getMemberCount() const;

  /** 
   * @brief Converts Channel mode (state) to the string and returns it in
   * the following format:
   * the string always has '+' at the beginnig, followed by a character.
   * Each character corresponds to a mode.
   * If a character is present - a mode is on. 
   * Example: '+i', '+itkl', '+'
   */
  std::string getModeString() const;
  /**
   * @brief returns a string with a key if present, 
   * followed by a number of users if the number of users is more than zero.
   * two fields are separeted with a space character ' '.
   */
  std::string getModeParams() const;

  /** 
   * @brief The member list as one or more space-separated chunks, each at most
   * `budget` bytes, so RPL_NAMREPLY can stay inside the 512-byte line
   * limit on a channel of any size. Always returns at least one chunk. 
   */
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

  /**
 * @brief add a Client to the channel.
 * This is done by adding a Client fd to Channel._inviteList.
 * Invite list is"  std::set<int> _inviteList.
 * 
 */
  void addInvite(Client* client);
  /**
   * @brief add a client to a Channel 
   */

  bool isInvited(Client* client) const;

  /**
   * @brief Remove a client from a Channel
   */
  void removeInvite(Client* client);



/* ─── Messaging ─── */

/**
 * @brief broadcast a clients message to all members of a Channel.
 * Iterates trough a _members map, 
 */
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
