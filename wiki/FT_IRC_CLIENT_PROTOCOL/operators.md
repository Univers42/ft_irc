To allow a reasonable amount of order to be kept within the IRC
   network, a special class of users (operators) is allowed to perform
   general maintenance functions on the network.  Although the powers
   granted to an operator can be considered as 'dangerous', they are
   nonetheless often necessary.  Operators SHOULD be able to perform
   basic network tasks such as disconnecting and reconnecting servers as
   needed.  In recognition of this need, the protocol discussed herein
   provides for operators only to be able to perform such functions.
   See sections 3.1.8 (SQUIT) and 3.4.7 (CONNECT).

   A more controversial power of operators is the ability to remove a
   user from the connected network by 'force', i.e., operators are able
   to close the connection between any client and server.  The
   justification for this is very delicate since its abuse is both
   destructive and annoying, and its benefits close to inexistent.  For
   further details on this type of action, see section 3.7.1 (KILL).