/*
 * esmini - Environment Simulator Minimalistic
 * https://github.com/esmini/esmini
 *
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at https://mozilla.org/MPL/2.0/.
 *
 * Copyright (c) partners of Simulation Scenarios
 * https://sites.google.com/view/simulationscenarios
 */

#include <iostream>
#include <string>
#include <random>

#include "ScenarioEngine.hpp"
#include "viewer.hpp"
#include "RoadManager.hpp"
#include "CommonMini.hpp"
#include "Server.hpp"

using namespace scenarioengine;

class ScenarioPlayer
{
	typedef enum
	{
		CONTROL_BY_OSC,
		CONTROL_INTERNAL,
		CONTROL_EXTERNAL,
		CONTROL_HYBRID
	} RequestControlMode;

public:

	std::string RequestControlMode2Str(RequestControlMode mode);
	void scenario_frame();
	void viewer_frame();
	int ScenarioPlayer::Init(int argc, char *argv[]);
	ScenarioPlayer(int argc, char *argv[]);

	const double maxStepSize;
	const double minStepSize;
	const double trail_dt;
	double simTime;
	ScenarioEngine *scenarioEngine;
	viewer::Viewer *viewer_;
	SE_Thread thread;
	SE_Mutex mutex;
	bool quit_request;
	bool threads;

};