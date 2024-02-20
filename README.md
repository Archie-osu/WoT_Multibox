# WoTMultibox
Allows for multiple instances of World of Tanks to run on a singular machine

# How does this work?
The World of Tanks client blocks launch of additional instances of the game by creating two additional mutant objects (``wgc_game_mtx_*`` and ``wgc_running_games_mtx``). The launcher checks for the existence of these mutants (they are system-wide) before allowing the game to be launched again.

The WoTMultibox application loops through all handles held by all active ``WorldOfTanks.exe`` processes, and closes any mutant handles matching the names above. 

# Will you hack my computer?
No. If you feel uncomfortable downloading random unsigned .exes from the internet, you can compile the project yourself using [Visual Studio 2022](https://visualstudio.microsoft.com).