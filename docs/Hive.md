# stoermelder HIVE

HIVE is a random ratcheting sequencer with 4 independent outputs which run on a common 2-dimensional hexagonal grid. HIVE is an oddball and a close relative of [MAZE](./Maze.md): Almost everything works the same way except for the playheads running on a grid consisting of hexagonal cells.

![HIVE Intro](./Hive-intro.gif)

HIVE was kindly contributed to PackOne by [Lexandra Maxine](https://github.com/xandramax) and this is what she has to say about the sequencer:  

> I've been intrigued by hexagonal grids for about as long as I can remember, largely driven by experiences with strategy games played on hexagonally-gridded game boards: Battle for Wesnoth, Settlers of Catan, Civilization 5, and recently games from Nick Bentley like Bug, Circle of Life, and Blooms.
> 
> In those scenarios which involve strategic positioning and movement, the six-sidedness of space creates a significant impact in the flow, feel, and apparent complexity of the game.
>
> I also think that hexagonal grids and the games played upon them are pretty.
> 
> I wanted to explore the interface between the visual appearance of a grid, a set of rules for movement, and any musical information derived from a combination of the two. In my exploration of MAZE, I typically began with an idea which I would then translate into visual intent upon the grid. After reacting to the audible result, I'd find myself reinterpereting the grid's appearance.
> 
> It's like catching sight of a cloud that looks like a frog, *listening* to that frog, and beginning to see that it really looks a bit more like a butterfly. Then you tweak the cloud.
> 
> When a sequencer is also a visualizer, through human interaction there can be found a feedback loop from the visual to the aural and the aural to the visual. Building probability and randomization into the sequencer adds fuel to the process.
> 
> Arriving at the concept for HIVE provided me with a unique opportunity to explore my hexagonal fascination. At the outset I wondered exactly how a grid transformation from square to hexagonal might translate to the music-space of a sequencer when accounting for MAZE’s laws of physics. I found that the result was worth the effort.
> 
> For one, the density of hexagonal grids translates to increased variation within any derived sequence. This means that the musical difference between a square grid of 8 musical notes and a hex grid of 7 notes is more nuanced than just one note. But it's also the case that the density of a hexagonal grid makes for a visual display of higher definition. It can be described as an increase in DPI. I think that's an asset for a sequencer which encourages interaction informed by visual perception.

A lush example of how HIVE and MAZE can work together:

[![HIVE and MAZE](https://img.youtube.com/vi/KYbfuj7EbbQ/0.jpg)](https://www.youtube.com/watch?v=KYbfuj7EbbQ)


## Changelog

- v1.8.0
  - Initial release of HIVE
- v1.9.0
  - Fixed hanging ratchets on missing or stopped clock trigger (#216)
  - Added new ratcheting modes ("Twos", "Threes", "Power of Two")
- v2.0.0
  - Fixed broken reset-behavior