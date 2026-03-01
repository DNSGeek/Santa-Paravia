# Santa-Paravia
Some assets and scripts for a cross-platform Santa Paravia game

Start the server:

bash
pip install fastapi uvicorn
uvicorn paravia_server:app --host 0.0.0.0 --port 8765
Build the client:

bash
# Single-player (unchanged behaviour)
gcc -O2 -o paravia paravia.c -lncurses

# Network client
gcc -O2 -DNETWORK_MODE -o paravia paravia.c -lncurses -lcurl -lpthread
Host a game (Player 0):

bash
./paravia --server http://yourserver:8765
```
It prompts for all player names, POSTs `/game/new`, then prints ready-to-paste join commands for everyone else like:
```
Player 1 (Lorenzo):
  ./paravia --server http://yourserver:8765 --join a3f2c1b0 \
            --player 1 --token 9e4f1d2a-...
Other players join:

bash
./paravia --server http://yourserver:8765 --join a3f2c1b0 --player 1 --token 9e4f1d2a-...
What happens during play:
The active player sees the full interactive TUI as before
Watching players see their own map/stats panel frozen, with events scrolling in the message log in real time as the active player buys grain, sets taxes, builds — the poll thread wakes within 50ms of each server event
Turn advances automatically when the active player hits q in the purchases screen
All game logic (harvest, prices, invasions, deaths, title checks) runs on the server so there's no possibility of clients disagreeing on state
