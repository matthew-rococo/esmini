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
#include "playerbase.hpp"

using namespace scenarioengine;

osg::Vec3 color_dark_gray = { 0.5, 0.5, 0.5 };
osg::Vec3 color_yellow = { 0.55, 0.46, 0.4 };
osg::Vec3 color_red = { 0.6, 0.2, 0.2 };

void log_callback(const char *str)
{
	printf("%s\n", str);
}

std::string ScenarioPlayer::RequestControlMode2Str(RequestControlMode mode)
{
	if (mode == RequestControlMode::CONTROL_BY_OSC) return "by OSC";
	else if (mode == RequestControlMode::CONTROL_INTERNAL) return "Internal";
	else if (mode == RequestControlMode::CONTROL_EXTERNAL) return "External";
	else if (mode == RequestControlMode::CONTROL_HYBRID) return "Hybrid";
	else return "Unknown";
}

ScenarioPlayer::ScenarioPlayer(int argc, char *argv[]) : 
	maxStepSize(0.1), minStepSize(0.01), trail_dt(0.5)
{
	simTime = 0.0;
	quit_request = false;
	threads = false;

	if (Init(argc, argv) != 0)
	{
		throw std::invalid_argument("Failed to initialize scenario player");
	}
}

ScenarioPlayer::~ScenarioPlayer()
{
	if (scenarioEngine->entities.object_[0]->GetControl() == Object::Control::EXTERNAL ||
		scenarioEngine->entities.object_[0]->GetControl() == Object::Control::HYBRID_EXTERNAL)
	{
		StopServer();
	}

	delete viewer_;
	delete scenarioEngine;
}

void ScenarioPlayer::Frame()
{
	if (!threads)
	{
		ScenarioFrame();
	}
	ViewerFrame();
}

void ScenarioPlayer::ScenarioFrame()
{
	static __int64 lastTimeStamp = 0;
	double deltaSimTime;
	__int64 now;

	// Get milliseconds since Jan 1 1970
	now = SE_getSystemTime();
	deltaSimTime = (now - lastTimeStamp) / 1000.0;  // step size in seconds
	lastTimeStamp = now;
	double adjust = 0;

	if (deltaSimTime > maxStepSize) // limit step size
	{
		adjust = -(deltaSimTime - maxStepSize);
	}
	else if (deltaSimTime < minStepSize)  // avoid CPU rush, sleep for a while
	{
		adjust = minStepSize - deltaSimTime;
		SE_sleep(adjust * 1000);
		lastTimeStamp += adjust * 1000;
	}

	deltaSimTime += adjust;

	// Time operations
	simTime = simTime + deltaSimTime;

	mutex.Lock();

	scenarioEngine->step(deltaSimTime);
	//LOG("%d %d %.2f h: %.5f road_h %.5f h_relative_road %.5f",
	//    scenarioEngine->entities.object_[0]->pos_.GetTrackId(),
	//    scenarioEngine->entities.object_[0]->pos_.GetLaneId(),
	//    scenarioEngine->entities.object_[0]->pos_.GetS(),
	//    scenarioEngine->entities.object_[0]->pos_.GetH(),
	//    scenarioEngine->entities.object_[0]->pos_.GetHRoad(),
	//    scenarioEngine->entities.object_[0]->pos_.GetHRelative());

	mutex.Unlock();
}

void ScenarioPlayer::ViewerFrame()
{
	static double last_dot_time = 0;

	bool add_dot = false;
	if (simTime - last_dot_time > trail_dt)
	{
		add_dot = true;
		last_dot_time = simTime;
	}

	mutex.Lock();

	// Visualize cars
	for (size_t i = 0; i < scenarioEngine->entities.object_.size(); i++)
	{
		viewer::CarModel *car = viewer_->cars_[i];
		Object *obj = scenarioEngine->entities.object_[i];
		roadmanager::Position pos = scenarioEngine->entities.object_[i]->pos_;

		car->SetPosition(pos.GetX(), pos.GetY(), pos.GetZ());
		car->SetRotation(pos.GetH(), pos.GetP(), pos.GetR());
		car->UpdateWheels(obj->wheel_angle_, obj->wheel_rot_);

		if (add_dot)
		{
			car->trail_->AddDot(simTime, obj->pos_.GetX(), obj->pos_.GetY(), obj->pos_.GetZ(), obj->pos_.GetH());
		}
	}

	// Update info text 
	static char str_buf[128];
	snprintf(str_buf, sizeof(str_buf), "%.2fs %.2fkm/h", simTime, 3.6 * scenarioEngine->entities.object_[viewer_->currentCarInFocus_]->speed_);
	viewer_->SetInfoText(str_buf);

	mutex.Unlock();

	viewer_->osgViewer_->frame();
	if (viewer_->osgViewer_->done())
	{
		quit_request = true;
	}
}

void scenario_thread(void *args)
{
	ScenarioPlayer *player = (ScenarioPlayer*)args;

	while (!player->IsQuitRequested())
	{
		player->ScenarioFrame();
	}
}

int ScenarioPlayer::Init(int argc, char *argv[])
{
	std::string arg_str;

	// Use logger callback
	Logger::Inst().SetCallback(log_callback);

	// use an ArgumentParser object to manage the program arguments.
	osg::ArgumentParser arguments(&argc, argv);

	arguments.getApplicationUsage()->setApplicationName(arguments.getApplicationName());
	arguments.getApplicationUsage()->setDescription(arguments.getApplicationName());
	arguments.getApplicationUsage()->setCommandLineUsage(arguments.getApplicationName() + " [options]\n");
	arguments.getApplicationUsage()->addCommandLineOption("--osc <filename>", "OpenSCENARIO filename");
	arguments.getApplicationUsage()->addCommandLineOption("--control <mode>", "Ego control (\"osc\", \"internal\", \"external\", \"hybrid\"");
	arguments.getApplicationUsage()->addCommandLineOption("--info_text <mode>", "Show info text HUD (\"on\" (default), \"off\") (toggle during simulation by press 't') ");
	arguments.getApplicationUsage()->addCommandLineOption("--trails <mode>", "Show trails (\"on\" (default), \"off\") (toggle during simulation by press 't') ");
	arguments.getApplicationUsage()->addCommandLineOption("--threads", "Run viewer and scenario in separate threads");

	if (arguments.argc() < 2)
	{
		arguments.getApplicationUsage()->write(std::cout, 1, 80, true);
		return -1;
	}

	std::string oscFilename;
	arguments.read("--osc", oscFilename);

	RequestControlMode control;
	arguments.read("--control", arg_str);
	if (arg_str == "osc" || arg_str == "") control = RequestControlMode::CONTROL_BY_OSC;
	else if (arg_str == "internal") control = RequestControlMode::CONTROL_INTERNAL;
	else if (arg_str == "external") control = RequestControlMode::CONTROL_EXTERNAL;
	else if (arg_str == "hybrid") control = RequestControlMode::CONTROL_HYBRID;
	else
	{
		LOG("Unrecognized external control mode: %s", arg_str.c_str());
		control = RequestControlMode::CONTROL_BY_OSC;
	}

	if (arguments.read("--threads"))
	{
		threads = true;
		LOG("Run viewer in separate thread");
	}

	// Create scenario engine
	try
	{
		scenarioEngine = new ScenarioEngine(oscFilename, 1.0, (ScenarioEngine::RequestControlMode)control);
	}
	catch (std::logic_error &e)
	{
		printf("%s\n", e.what());
		return -1;
	}

	// ScenarioGateway
	ScenarioGateway *scenarioGateway = scenarioEngine->getScenarioGateway();

	// Create a data file for later replay?
	if (arguments.read("--record", arg_str))
	{
		LOG("Recording data to file %s", arg_str.c_str());
		scenarioGateway->RecordToFile(arg_str, scenarioEngine->getOdrFilename(), scenarioEngine->getSceneGraphFilename());
	}

	// Step scenario engine - zero time - just to reach and report init state of all vehicles
	scenarioEngine->step(0.0, true);

	// Create viewer
	viewer_ = new viewer::Viewer(
		roadmanager::Position::GetOpenDrive(),
		scenarioEngine->getSceneGraphFilename().c_str(),
		scenarioEngine->getScenarioFilename().c_str(),
		arguments);

	arguments.read("--info_text", arg_str);
	if (arg_str == "off")
	{
		viewer_->ShowInfoText(false);
	}

	arguments.read("--trails", arg_str);
	if (arg_str == "off")
	{
		viewer_->ShowTrail(false);
	}

	//  Create cars for visualization
	for (size_t i = 0; i < scenarioEngine->entities.object_.size(); i++)
	{
		//  Create vehicles for visualization
		osg::Vec3 trail_color;
		Object *obj = scenarioEngine->entities.object_[i];

		if (obj->control_ == Object::Control::HYBRID_GHOST)
		{
			trail_color.set(color_dark_gray[0], color_dark_gray[1], color_dark_gray[2]);
		}
		else if (obj->control_ == Object::Control::HYBRID_EXTERNAL || obj->control_ == Object::Control::EXTERNAL)
		{
			trail_color.set(color_yellow[0], color_yellow[1], color_yellow[2]);
		}
		else
		{
			trail_color.set(color_red[0], color_red[1], color_red[2]);
		}

		bool transparent = obj->control_ == Object::Control::HYBRID_GHOST ? true : false;
		if (viewer_->AddCar(obj->model_filepath_, transparent, trail_color) == 0)
		{
			delete viewer_;
			viewer_ = 0;
			return -1;
		}
	}

	if (scenarioEngine->entities.object_[0]->GetControl() == Object::Control::EXTERNAL ||
		scenarioEngine->entities.object_[0]->GetControl() == Object::Control::HYBRID_EXTERNAL)
	{
		// Launch UDP server to receive external Ego state
		StartServer(scenarioEngine);
	}

	if (threads)
	{
		// Launch scenario engine in a separate thread
		thread.Start(scenario_thread, this);
	}

	return 0;
}
