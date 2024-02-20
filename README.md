# WoTMultibox
Allows for multiple instances of World of Tanks to run on a singular machine

# How does this work? Can I get banned for this?
The World of Tanks client blocks launch of additional instances of the game by creating two additional mutant objects (``wgc_game_mtx_*`` and ``wgc_running_games_mtx``). The launcher checks for the existence of these mutants (they are system-wide) before allowing the game to be launched again.

The WoTMultibox application loops through all handles held by all active ``WorldOfTanks.exe`` processes, and closes any mutant handles matching the names above.

Unlike other projects, the WoTMultibox application does **not** modify the game in any way (be it patching the executable, or reading / writing game memory), which makes it 100% anti-cheat friendly.
Minimal access to the game is required (only the ``Query Information`` and ``Duplicate Handles`` rights are necessary).

# How do I use this?
Simply let the application run in the background and play like you normally would. 
It's not necessary to start it prior to opening the game, so you can choose to use the multiboxing capabilities only when you truly need them.  

# Will you hack my computer?
No. If you feel uncomfortable downloading random unsigned .exes from the internet, you can compile the project yourself using [Visual Studio 2022](https://visualstudio.microsoft.com), as it comes with full source code and project files.
