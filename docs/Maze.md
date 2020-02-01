# stoermelder MAZE

MAZE is a random ratcheting sequencer with 4 independent outputs which run on a common 2-dimensional grid. This module is all about randomness and limitations, so you can't control everything.

[Live stream](https://www.youtube.com/watch?v=pOcYGj1Qb9M) of [Omri Cohen](https://omricohencomposer.bandcamp.com/) using Maze for sequencing.

### The grid

The grid is the main part of MAZE with up to four color-coded "cursors". Cursors are visible and enabled if one of their ports CV or TRIG outputs are connected. Each cell of the grid can have one of three states:

- Off: No trigger is raised when a cursor enters the cell.
- Filled: A trigger is raised on every entering.
- Half: The incoming clock signal is multiplied by a randomly chosen factor and multiple gate pulses are sent to the TRIG-port generating a ratcheting sequence. Although the exact number of triggers cannot be controlled there is a probability setting on the context menu to influence the  geometric distribution in use for generating the number of gates: Higher percent-values mean that bigger factors of clock multiples are more likely.

If "Ratcheting" is disabled by context menu option each half filled cell generates one single gate pulse with probability of 50%.

![MAZE ratcheting probability](./Maze-ratchet.png)

A mouse click into a grid cell cycles through the three different states. Additionally each cell holds a randomly choosen CV value that is sent to the CV port everytime a trigger is raised. The CV value of a cell cannot be changed manually but each cycle of the cell-states sets a new value. While the number of triggers of half cells is randomly chosen everytime a cursor enters them the CV values are constant.

The grid can be sized from 2x2 up to 32x32 with the slider on the context menu. The cells can also be randomized or randomized with "certainty" which generates no half filled cells.

### CLK and RESET ports

Each cursor has his own clock and reset port. Every clock-trigger moves the cursor one cell forward according to the current progressing direction. A reset-trigger returns the cursor to its start position which can be set on the Edit-mode of the grid. Triggers on CLK and RESET of the yellow channel are normalized to the other ports if no cable is connected.

![MAZE clock and reset](./Maze-clock.gif)

### TURN ports 

A trigger on the TURN-port turns the current progressing direction of the cursor 90 degrees to the right or 180 degrees (this setting is found in Edit-mode). Triggers of the yellow channel are normalized to the other ports.

![MAZE turn](./Maze-turn.gif)

### SHIFT/L and SHIFT/R ports

Triggers on SHIFT/L or SHIFT/R shift all cursors one lane to the left or to the right, respectively, according to the current progressing direction. It is a deliberate limitation of the module that shifts are only possible on all channels the same time.

### Edit-mode of the grid

![MAZE Edit-mode](./Maze-edit1.gif)

The grid can be switched to "Edit-mode" on the context menu. While in Edit-mode the reset-positions of each cursor can be modified by drag and drop. Everytime RESET is triggered the respective cursor will return to this cell on the grid.

There are some additional settings on the context menu of each cursor:

- Start progressing direction: Right (default), Left, Up, Down
- Behavior of TURN-triggers: 90 degrees (default) or 180 degrees
- Output range of the respective CV-port, default is 0..3V

![MAZE Edit-mode context menu](./Maze-edit2.png)

### Bonus tips

- The module works with Rack's undo-functionality for all changes of the grid.
- Some deliberate limitations have been set for this module:
  - The grid can’t be randomized or changed by CV.
  - The size of the grid can't be changed by CV.
  - You have no control over ratchets (but they can be disabled completely).
  - 90 degree-turns are only to the right.
  - CV values cannot be set manually.
- The module continues its normal operation while in Edit-mode.

MAZE was added in v1.3.0 of PackOne.