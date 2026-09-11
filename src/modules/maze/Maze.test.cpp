#include "Maze.test.hpp"

namespace __module {
	#include "Maze.test.module.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelMaze);
}
