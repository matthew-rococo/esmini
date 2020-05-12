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

#pragma once

#include "OSCGlobalAction.hpp"
#include "OSCPrivateAction.hpp"
#include "OSCParameterDeclarations.hpp"
#include "OSCConditionGroup.hpp"
#include "CommonMini.hpp"

#include <iostream>
#include <string>
#include <vector>

namespace scenarioengine
{

	class Event
	{
	public:

		typedef enum
		{
			OVERWRITE,
			FOLLOWING,
			SKIP
		} Priority;

		typedef enum
		{
			INACTIVE,
			ACTIVATED,      // Just activated - this state last for one step
			ACTIVE,  
			DEACTIVATED,    // Just done/deactivated - this state last for one step
			WAITING         // WIll be activated after ongoing event is done - only one waiting supported, latest in
		} State;

		State state_;
		std::string name_;
		Priority priority_;

		std::vector<OSCAction*> action_;

		std::vector<OSCConditionGroup*> start_condition_group_;

		Event() : state_(State::INACTIVE) {}

		bool IsActive()
		{
			return state_ == State::ACTIVATED || state_ == State::ACTIVE;
		}

		void Trig();
		void Stop();
		bool Triggable();
	};

	class OSCManeuver
	{
	public:
		OSCParameterDeclarations parameter_declarations_;
		std::vector<Event*> event_;
		std::string name_;

		int GetActiveEventIdx();
		int GetWaitingEventIdx();

		void Print()
		{
			LOG("\tname = %s", name_.c_str());
		};
	};

}