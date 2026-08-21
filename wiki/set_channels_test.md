For example, you could organize them like this:

#test-right — normal/correct behavior
#test-null — null or empty values
#test-invalid — invalid input or commands
#test-edge — boundary and edge cases
#test-error — expected error handling
#test-multiuser — scenarios involving multiple users
#test-auth — passwords, operators, permissions
#test-join-part — JOIN/PART behavior
#test-kick — KICK command tests
#test-privmsg — private messages
#test-nick — nickname-related tests
#test-channel-modes — channel modes and restrictions

If you both have your own separate channels, you could add initials or names:

#test-right-alice
#test-right-bob
#test-null-alice
#test-null-bob

Or, perhaps cleaner, organize them by scenario first:

#alice-valid
#alice-null
#alice-edge
#colleague-valid
#colleague-invalid
#colleague-multiuser

My recommendation would be to keep the names short and test-focused, for example:

#test-<command>-<scenario>

Such as:

#test-join-valid
#test-join-invalid
#test-join-null
#test-kick-valid
#test-kick-edge

That gives you a very clear structure while testing ft_irc.
