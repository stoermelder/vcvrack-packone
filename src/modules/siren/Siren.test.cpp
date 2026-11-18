#include "../../test/framework.hpp"
#include "Siren.cpp"
#include "Siren.test.hpp"
#include "DataSource.hpp"
#include "BpmDetector.hpp"
#include "FileSystem.hpp"
#include "BackgroundTasks.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <random>
#include <sstream>
#include <vector>

using namespace StoermelderPackOne::Siren;
using namespace StoermelderPackOne::Siren::filesystem;

Test::TestContext<> testContext;

namespace __module {
	#include "Siren.test.module.hpp"
}
namespace __audio {
	#include "Siren.test.audio.hpp"
}
namespace __bpm {
	#include "Siren.test.bpmdetector.hpp"
}
namespace __filesystem {
	#include "Siren.test.filesystem.hpp"
}
namespace __tasks {
	#include "Siren.test.tasks.hpp"
}

void testPluginInit(rack::Plugin* p) {
	pluginInstance = p;
	p->addModel(modelSiren);
}