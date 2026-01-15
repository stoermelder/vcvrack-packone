#include "../../test/test_plugin.hpp"
#include "../../test/test_context.hpp"
#include "AhabSim.hpp"

using namespace StoermelderPackOne;
using namespace StoermelderPackOne::Ahab;

// Define the single instance used by tests
static Test::TestContext<> testContext;


extern "C" Usz custom_vcvin(void* ptr, Usz port_num, Usz a, Usz b);
extern "C" void custom_vcvout(void* ptr, Usz port_index, Usz a, Usz b, Usz value);

TEST_CASE("Op vcvin ports 1-4 map to voltage range", "[AhabSim]") {
	AhabSim sim;
	size_t in_port = 0.f; float in_voltage = 10.0f;

	// Port 1 maps to dsp input index 0
	sim.setDspInputReader([&](size_t p){ return p == in_port ? in_voltage : 5.0f; });
	void* ptr = (void*)sim.getEvents();
	// port 1: 10v, mapping should saturate to upper bound -> expect 15 for range [5,15]
	REQUIRE(custom_vcvin(ptr, 1, 5, 15) == 15);
	// port 2, 3, 4: 5v input -> expect mid value 12 for range [5,15]
	REQUIRE(custom_vcvin(ptr, 2, 5, 15) == 10);
	REQUIRE(custom_vcvin(ptr, 3, 5, 15) == 10);
	REQUIRE(custom_vcvin(ptr, 4, 5, 15) == 10);

	// With input=0v, expect lower bound
	sim.setDspInputReader([](size_t p){ return 0.0f; });
	REQUIRE(custom_vcvin(ptr, 1, 5, 15) == 5);
	REQUIRE(custom_vcvin(ptr, 2, 5, 15) == 5);
	REQUIRE(custom_vcvin(ptr, 3, 5, 15) == 5);
	REQUIRE(custom_vcvin(ptr, 4, 5, 15) == 5);
}

TEST_CASE("Op vcvin ports A-D map to semitone mod12", "[AhabSim]") {
	AhabSim sim;
	size_t in_port = 0; float in_voltage = 0.75f;

	// Letter port 10 maps to dsp input index 0
	sim.setDspInputReader([&](size_t p){ return p == in_port ? in_voltage : 0.0f; });
	void* ptr = (void*)sim.getEvents();
	// port A (10): 0.75 * 12 = 9 -> rounds to 9
	REQUIRE(custom_vcvin(ptr, 10, 0, 0) == 9);
	// port B, C, D (11-13): 0.0 * 12 = 0
	REQUIRE(custom_vcvin(ptr, 11, 0, 0) == 0);
	REQUIRE(custom_vcvin(ptr, 12, 0, 0) == 0);
	REQUIRE(custom_vcvin(ptr, 13, 0, 0) == 0);
}

TEST_CASE("Op vcvout ports 1-4 write scaled voltages", "[AhabSim]") {
	AhabSim sim;
	size_t out_port = 99; float out_voltage = 0.0f;
	void* ptr = (void*)sim.getEvents();
	sim.setDspOutputWriter([&](size_t p, float v){ out_port = p; out_voltage = v;});

	// Clamp value=35 in range [0,35] -> voltage should be 10.0f
	custom_vcvout(ptr, 1, 0, 35, 35);
	REQUIRE(out_port == 0);
	REQUIRE(out_voltage == Approx(10.0f));

	// Test mid value: value=20 in range [10,30] -> voltage should be 5v
	custom_vcvout(ptr, 2, 10, 30, 20);
	REQUIRE(out_port == 1);
	REQUIRE(out_voltage == Approx(5.0f));

	// Test lower bound: value=0 in range [5,25] -> voltage should be 0v
	custom_vcvout(ptr, 3, 5, 25, 0);
	REQUIRE(out_port == 2);
	REQUIRE(out_voltage == Approx(0.0f));

	// Test upper bound: value=20 in range [5,15] -> voltage should be 10v
	custom_vcvout(ptr, 4, 5, 15, 20);
	REQUIRE(out_port == 3);
	REQUIRE(out_voltage == Approx(10.0f));
}

TEST_CASE("Op vcvout ports A-D write v/oct conversion", "[AhabSim]") {
	AhabSim sim;
	size_t out_port = 99; float out_voltage = 0.0f;
	void* ptr = (void*)sim.getEvents();
	sim.setDspOutputWriter([&](size_t p, float v){ out_port = p; out_voltage = v; });
	
	// Letter port A (10) -> port 0. For a=1, value=3 -> (3 + 1*12)/12 = 1.25
	custom_vcvout(ptr, 10, 1, 0, 3);
	REQUIRE(out_port == 0);
	REQUIRE(out_voltage == Approx(1.f + 3 * 1.f / 12.f));

	// Letter port B (11) -> port 1. For a=0, value=0 -> (0 + 0*12)/12 = 0.0
	custom_vcvout(ptr, 11, 0, 0, 0);
	REQUIRE(out_port == 1);
	REQUIRE(out_voltage == Approx(0.0f));

	// Letter port C (12) -> port 2. For a=2, value=6 -> (6 + 2*12)/12 = 2.5
	custom_vcvout(ptr, 12, 2, 0, 12);
	REQUIRE(out_port == 2);
	REQUIRE(out_voltage == Approx(3.f));
}
