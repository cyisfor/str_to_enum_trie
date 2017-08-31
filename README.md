Can intern strings VERY efficiently, by taking a fixed list of allowed strings, and forming a trie, made out of C code, not data. It switches over the first letter, then into the second, and so on, until hitting the null terminator.

The generated code should be fairly human readable, so take a look at that to see just what it’s doing.

Could alter this to take code for a bounded string that contained nulls, but then each switch would have to be wrapped in an if() to account for if we’re at the end of the word, instead of just `case '\0'`. The benefit would be that for long suffixes, we could use memcmp instead of strncmp.
