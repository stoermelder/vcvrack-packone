#include "plugin.hpp"


Plugin *pluginInstance;

void init(rack::Plugin *p) {
	pluginInstance = p;

	p->addModel(modelCV_Map);
	p->addModel(modelCV_Pam);
	p->addModel(modelRotorA);
	p->addModel(modelRePlay);
}