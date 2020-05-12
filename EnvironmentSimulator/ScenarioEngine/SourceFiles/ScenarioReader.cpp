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

#include "ScenarioReader.hpp"
#include "CommonMini.hpp"

#include <cstdlib>

namespace {
	int strtoi(std::string s) {
		return atoi(s.c_str());
	}

	double strtod(std::string s) {
		return atof(s.c_str());
	}
}

using namespace scenarioengine;

void ScenarioReader::addParameterDeclarations(pugi::xml_node xml_node)
{
	parseParameterDeclarations(xml_node, &parameterDeclarations_);
}

void ScenarioReader::parseGlobalParameterDeclarations()
{
	parseParameterDeclarations(doc_.child("OpenSCENARIO").child("ParameterDeclarations"), &parameterDeclarations_);
	paramDeclarationsSize_ = (int)parameterDeclarations_.Parameter.size();
}

void ScenarioReader::RestoreParameterDeclarations()
{
	parameterDeclarations_.Parameter.erase(
		parameterDeclarations_.Parameter.begin(), 
		parameterDeclarations_.Parameter.begin() + parameterDeclarations_.Parameter.size() - paramDeclarationsSize_);
	catalog_param_assignments.clear();
}

void ScenarioReader::addParameter(std::string name, std::string value)
{
	ParameterStruct param;

	LOG("adding %s = %s", name.c_str(), value.c_str());

	param.name = name;
	param.type = "string";
	param.value = value;

	parameterDeclarations_.Parameter.insert(parameterDeclarations_.Parameter.begin(), param);
}

std::string ScenarioReader::getParameter(OSCParameterDeclarations &parameterDeclaration, std::string name)
{
	LOG("Resolve parameter %s", name.c_str());

	// If string already present in parameterDeclaration
	for (size_t i = 0; i < parameterDeclaration.Parameter.size(); i++)
	{
		if (parameterDeclaration.Parameter[i].name == name)
		{
			LOG("%s replaced with %s", name.c_str(), parameterDeclaration.Parameter[i].value.c_str());
			return parameterDeclaration.Parameter[i].value;
		}
	}
	LOG("Failed to resolve parameter %s", name.c_str());
	throw std::runtime_error("Failed to resolve parameter");
	return 0;
}

std::string ScenarioReader::ReadAttribute(pugi::xml_node node, std::string attribute_name, bool required)
{
	if (!strcmp(attribute_name.c_str(), ""))
	{
		if (required)
		{
			LOG("Warning: Empty attribute");
		}
		return "";
	}

	pugi::xml_attribute attr;
	
	if ((attr = node.attribute(attribute_name.c_str())))
	{
		if (attr.value()[0] == '$')
		{
			// Resolve variable
			return getParameter(parameterDeclarations_, attr.value());
		}
		else
		{
			return attr.value();
		}
	}
	else
	{
		if (required)
		{
			LOG("Error: missing required attribute: %s", attribute_name.c_str());
		}
		else
		{
			LOG("Warning: missing attribute: %s", attribute_name.c_str());
		}
	}

	return "";
}

int ScenarioReader::loadOSCFile(const char * path)
{
	LOG("Loading %s", path);

	pugi::xml_parse_result result = doc_.load_file(path);
	if (!result)
	{
		LOG("Error: %s", result.description());
		return -1;
	}

	oscFilename_ = path;

	return 0;
}

void ScenarioReader::loadOSCMem(const pugi::xml_document &xml_doc)
{
	LOG("Loading XML document from memory");

	doc_.reset(xml_doc);
	oscFilename_ = "inline";
}

int ScenarioReader::RegisterCatalogDirectory(pugi::xml_node catalogDirChild)
{
	if (catalogDirChild.child("Directory") == NULL)
	{
		LOG("Catalog %s sub element Directory not found - skipping", catalogDirChild.name());
		return -1;
	}

	std::string dirname = ReadAttribute(catalogDirChild.child("Directory"), "path", true);

	if (dirname == "")
	{
		LOG("Catalog %s missing filename - ignoring", catalogDirChild.name());
		return -1;
	}

	// Directory name should be relative the XOSC file
	std::string catalog_dir = CombineDirectoryPathAndFilepath(DirNameOf(oscFilename_), dirname);

	catalogs_->RegisterCatalogDirectory(catalogDirChild.name(), catalog_dir);
	
	return 0;
}

Catalog* ScenarioReader::LoadCatalog(std::string name)
{
	Catalog *catalog;

	// Check if already loaded
	if ((catalog = catalogs_->FindCatalogByName(name)) != 0)
	{
		// Catalog already loaded
		return catalog;
	}

	// Not found, try to locate it in one the registered catalog directories 
	pugi::xml_document catalog_doc;
	size_t i;
	for (i = 0; i < catalogs_->catalog_dirs_.size(); i++)
	{
		std::string file_path = catalogs_->catalog_dirs_[i].dir_name_ + "/" + name + ".xosc";

		// Load it
		pugi::xml_parse_result result = catalog_doc.load_file(file_path.c_str());

		if (result)
		{
			break;
		}
	}

	if (i == catalogs_->catalog_dirs_.size())
	{
		LOG("Couldn't locate catalog file %s make sure it is located in one of the catalog directories listed in the scenario file", name.c_str());
		return 0;
	}

	LOG("Loading catalog %s", name.c_str());
	pugi::xml_node catalog_node = catalog_doc.child("OpenSCENARIO").child("Catalog");

	catalog = new Catalog();
	catalog->name_ = name;

	for (pugi::xml_node entry_n = catalog_node.first_child(); entry_n; entry_n = entry_n.next_sibling())
	{
		std::string entry_name = ReadAttribute(entry_n, "name");
		
		// To copy a XML node it needs to be put into a XML doc
		pugi::xml_document *xml_doc = new pugi::xml_document;
		xml_doc->append_copy(entry_n);
		catalog->AddEntry(new Entry(entry_name, xml_doc->first_child()));
	}
	
	// Get type by inspecting first entry
	if (catalog->entry_.size() > 0)
	{
		catalog->type_ = catalog->entry_[0]->type_;
	}
	else
	{
		LOG("Warning: Catalog %s seems to be empty!", catalog->name_.c_str());
	}

	catalogs_->AddCatalog(catalog);

	return catalog;
}

void ScenarioReader::parseParameterDeclarations(pugi::xml_node parameterDeclarationsNode, OSCParameterDeclarations *pd)
{
	LOG("Parsing ParameterDeclarations");

	for (pugi::xml_node pdChild = parameterDeclarationsNode.first_child(); pdChild; pdChild = pdChild.next_sibling())
	{
		ParameterStruct param;

		param.name = pdChild.attribute("name").value();
		
		// Check for catalog parameter assignements, overriding default value
		param.value = pdChild.attribute("value").value();
		for (size_t i = 0; i < catalog_param_assignments.size(); i++)
		{
			if (param.name == catalog_param_assignments[i].name)
			{
				param.value = catalog_param_assignments[i].value;
				break;
			}
		}
		param.type = pdChild.attribute("parameterType").value();
		pd->Parameter.insert(pd->Parameter.begin(), param);
	}
}

void ScenarioReader::parseRoadNetwork(RoadNetwork &roadNetwork)
{
	LOG("Parsing RoadNetwork");

	pugi::xml_node roadNetworkNode = doc_.child("OpenSCENARIO").child("RoadNetwork");

	for (pugi::xml_node roadNetworkChild = roadNetworkNode.first_child(); roadNetworkChild; roadNetworkChild = roadNetworkChild.next_sibling())
	{
		std::string roadNetworkChildName(roadNetworkChild.name());

		if (roadNetworkChildName == "LogicFile")
		{
			parseOSCFile(roadNetwork.logicFile, roadNetworkChild);
		}
		else if (roadNetworkChildName == "SceneGraphFile")
		{
			parseOSCFile(roadNetwork.sceneGraphFile, roadNetworkChild);
		}
	}

	if (roadNetwork.logicFile.filepath == "")
	{
		LOG("Warning: No road network ODR file loaded!");
	}
	
	if (roadNetwork.sceneGraphFile.filepath == "")
	{
		LOG("Warning: No road network 3D model file loaded! Setting default path.");

		// Since the scene graph file path is used to locate other 3D files, like vehicles, create a default path 
		roadNetwork.sceneGraphFile.filepath = DirNameOf(oscFilename_);
	}

	LOG("Roadnetwork ODR: %s", roadNetwork.logicFile.filepath.c_str());
	LOG("Scenegraph: %s", roadNetwork.sceneGraphFile.filepath.c_str());
}

void ScenarioReader::ParseOSCProperties(OSCProperties &properties, pugi::xml_node &xml_node)
{
	pugi::xml_node properties_node = xml_node.child("Properties");
	if (properties_node != NULL)
	{
		for (pugi::xml_node propertiesChild = properties_node.first_child(); propertiesChild; propertiesChild = propertiesChild.next_sibling())
		{
			if (!strcmp(propertiesChild.name(), "File"))
			{
				properties.file_.filepath_ = ReadAttribute(propertiesChild, "filepath");
				if (properties.file_.filepath_ != "")
				{
					LOG("Properties/File = %s registered", properties.file_.filepath_.c_str());
				}
			}
			else if (!strcmp(propertiesChild.name(), "Property"))
			{
				OSCProperties::Property property;
				property.name_ = ReadAttribute(propertiesChild, "name");
				property.value_ = ReadAttribute(propertiesChild, "value");
				properties.property_.push_back(property);
				LOG("Property %s = %s registered", property.name_.c_str(), property.value_.c_str());
			}
			else
			{
				LOG("Unexpected property element: %s", propertiesChild.name());
			}
		}
	}
}

Vehicle* ScenarioReader::createRandomOSCVehicle(std::string name)
{
	Vehicle *vehicle = new Vehicle();

	vehicle->name_ = name;
	vehicle->category_ = Vehicle::Category::CAR;
	vehicle->control_ = Object::Control::INTERNAL;
	vehicle->model_id_ = -1;
	vehicle->model_filepath_ = "";

	return vehicle;
}

Vehicle* ScenarioReader::parseOSCVehicle(pugi::xml_node vehicleNode)
{
	Vehicle *vehicle = new Vehicle();

	if (vehicleNode == 0)
	{
		return 0;
	}
	vehicle->name_ = ReadAttribute(vehicleNode, "name");
	LOG("Parsing Vehicle %s", vehicle->name_.c_str());
	vehicle->SetCategory(ReadAttribute(vehicleNode, "vehicleCategory"));

	OSCProperties properties;
	ParseOSCProperties(properties, vehicleNode);

	for(size_t i=0; i<properties.property_.size(); i++)
	{
		// Check if the property is something supported
		if (properties.property_[i].name_ == "control")
		{
			if (properties.property_[i].value_ == "internal")
			{
				vehicle->control_ = Object::Control::INTERNAL;
			}
			else if (properties.property_[i].value_ == "external")
			{
				vehicle->control_ = Object::Control::EXTERNAL;
			}
			else if (properties.property_[i].value_ == "hybrid")
			{
				// This will be the ghost vehicle, controlled by scenario engine,
				// which the externally controlled vehicle will follow
				vehicle->control_ = Object::Control::HYBRID_GHOST;  
			}
			else
			{
				vehicle->control_ = Object::Control::UNDEFINED;
			}
		}
		else if (properties.property_[i].name_ == "model_id")
		{
			vehicle->model_id_ = strtoi(properties.property_[i].value_);
		}
		else
		{
			LOG("Unsupported property: %s", properties.property_[i].name_.c_str());
		}
	}

	if (properties.file_.filepath_ != "")
	{
		vehicle->model_filepath_ = properties.file_.filepath_;
	}

	return vehicle;
}

roadmanager::Route* ScenarioReader::parseOSCRoute(pugi::xml_node routeNode)
{
	roadmanager::Route *route = new roadmanager::Route;

	route->setName(ReadAttribute(routeNode, "name"));

	LOG("Parsing OSCRoute %s", route->getName().c_str());

	// Closed attribute not supported by roadmanager yet
	std::string closed_str = ReadAttribute(routeNode, "closed");
	bool closed = false;
	(void)closed;
	if (closed_str == "true" || closed_str == "1")
	{
		closed = true;
	}

	for (pugi::xml_node routeChild = routeNode.first_child(); routeChild; routeChild = routeChild.next_sibling())
	{
		std::string routeChildName(routeChild.name());

		if (routeChildName == "ParameterDeclaration")
		{
			LOG("%s is not implemented", routeChildName.c_str());

		}
		else if (routeChildName == "Waypoint")
		{
			OSCPosition *pos = parseOSCPosition(routeChild.first_child());
			if (pos)
			{
				route->AddWaypoint(pos->GetRMPos());
			}
			else
			{
				LOG("Failed to parse waypoint position");
			}
		}
	}

	return route;
}

void ScenarioReader::parseCatalogs()
{
	LOG("Parsing Catalogs");

	pugi::xml_node catalogsNode = doc_.child("OpenSCENARIO").child("CatalogLocations");

	for (pugi::xml_node catalogsChild = catalogsNode.first_child(); catalogsChild; catalogsChild = catalogsChild.next_sibling())
	{
		RegisterCatalogDirectory(catalogsChild);
	}
}

void ScenarioReader::parseOSCFile(OSCFile &file, pugi::xml_node fileNode)
{
	LOG("Parsing OSCFile %s", file.filepath.c_str());

	file.filepath = CombineDirectoryPathAndFilepath(DirNameOf(oscFilename_), ReadAttribute(fileNode, "filepath"));

}

Entry* ScenarioReader::ResolveCatalogReference(pugi::xml_node node)
{
	std::string catalog_name;
	std::string entry_name;

	pugi::xml_node parameterAssignmentNode = node.child("ParameterAssignment");

	// Read any parameter assignments
	for (pugi::xml_node param_n = parameterAssignmentNode.child("Parameter"); param_n; param_n = param_n.next_sibling("Parameter"))
	{
		ParameterStruct param;
		param.name = param_n.attribute("name").value();
		param.value = ReadAttribute(param_n, "value");
		catalog_param_assignments.push_back(param);
	}

	catalog_name = ReadAttribute(node, "catalogName");
	entry_name = ReadAttribute(node, "entryName");

	if (catalog_param_assignments.size() > 0)
	{
		LOG("Assigned %d parameters for catalog %s entry %s", catalog_param_assignments.size(), catalog_name.c_str(), entry_name.c_str());
	}

	Catalog *catalog;

	// Make sure the catalog item is loaded
	if ((catalog = LoadCatalog(catalog_name)) == 0)
	{
		LOG("Failed to load catalog %s",  catalog_name.c_str());
		return 0;
	}

	Entry *entry = catalog->FindEntryByName(entry_name);
	if (entry == 0)
	{
		LOG("Failed to look up entry %s in catalog %s", entry_name.c_str(), catalog_name.c_str());

		return 0;
	}

	return entry;
}

int ScenarioReader::parseEntities()
{
	LOG("Parsing Entities");

	pugi::xml_node enitiesNode = doc_.child("OpenSCENARIO").child("Entities");

	for (pugi::xml_node entitiesChild = enitiesNode.first_child(); entitiesChild; entitiesChild = entitiesChild.next_sibling())
	{
		Object *obj = 0;

		for (pugi::xml_node objectChild = entitiesChild.first_child(); objectChild; objectChild = objectChild.next_sibling())
		{
			std::string objectChildName(objectChild.name());
			
			if (objectChildName == "CatalogReference")
			{
				Entry *entry = ResolveCatalogReference(objectChild);

				if (entry == 0)
				{
					// Invalid catalog reference - create random vehicle as fall-back
					LOG("Could not find catalog vehicle, creating a random car as fall-back");
					std::string entry_name = ReadAttribute(objectChild, "entryName");
					Vehicle *vehicle = createRandomOSCVehicle(entry_name);
					obj = vehicle;
				}
				else
				{
					if (entry->type_ == CatalogType::CATALOG_VEHICLE)
					{
						// Make a new instance from catalog entry 
						Vehicle *vehicle = parseOSCVehicle(entry->GetNode());
						obj = vehicle;
					}
					else
					{
						LOG("Unexpected catalog type %s", entry->GetTypeAsStr().c_str());
					}
				}

				RestoreParameterDeclarations();
			}
			else if (objectChildName == "Vehicle")
			{				
				Vehicle *vehicle = parseOSCVehicle(objectChild);
				obj = vehicle;
			}
			else
			{
				LOG("%s not supported yet", objectChildName.c_str());
			}
		}
		
		if (obj != 0)
		{
			obj->name_ = ReadAttribute(entitiesChild, "name");
			obj->id_ = (int)entities_->object_.size();
			entities_->object_.push_back(obj);
			objectCnt_++;
		}
	}

	return 0;
}

void ScenarioReader::parseOSCOrientation(OSCOrientation &orientation, pugi::xml_node orientationNode)
{
	orientation.h_ = strtod(ReadAttribute(orientationNode, "h"));
	orientation.p_ = strtod(ReadAttribute(orientationNode, "p"));
	orientation.r_ = strtod(ReadAttribute(orientationNode, "r"));

	std::string type_str = ReadAttribute(orientationNode, "type");

	if (type_str == "relative")
	{
		orientation.type_ = OSCOrientation::OrientationType::RELATIVE;
	}
	else if (type_str == "absolute")
	{
		orientation.type_ = OSCOrientation::OrientationType::ABSOLUTE;
	}
	else if (type_str == "")
	{
		LOG("No orientation type specified - using absolute");
		orientation.type_ = OSCOrientation::OrientationType::ABSOLUTE;
	}
	else
	{
		LOG("Invalid orientation type: %d - using absolute", type_str.c_str());
		orientation.type_ = OSCOrientation::OrientationType::ABSOLUTE;
	}
}

OSCPosition *ScenarioReader::parseOSCPosition(pugi::xml_node positionNode)
{
	OSCPosition *pos_return = 0;

	pugi::xml_node positionChild = positionNode.first_child();

	LOG("Parsing %s: ", positionChild.name());

	std::string positionChildName(positionChild.name());

	if (positionChildName == "WorldPosition")
	{

		double x = strtod(ReadAttribute(positionChild, "x"));
		double y = strtod(ReadAttribute(positionChild, "y"));
		double z = strtod(ReadAttribute(positionChild, "z"));
		double h = strtod(ReadAttribute(positionChild, "h"));
		double p = strtod(ReadAttribute(positionChild, "p"));
		double r = strtod(ReadAttribute(positionChild, "r"));

		OSCPositionWorld *pos = new OSCPositionWorld(x, y, z, h, p, r);

		pos_return = (OSCPosition*)pos;
	}
	else if (positionChildName == "RelativeWorldPosition")
	{
		LOG("%s is not implemented ", positionChildName.c_str());
	}
	else if (positionChildName == "RelativeObjectPosition")
	{
		double dx, dy, dz;
			
		dx = strtod(ReadAttribute(positionChild, "dx"));
		dy = strtod(ReadAttribute(positionChild, "dy"));
		dz = strtod(ReadAttribute(positionChild, "dz"));
		Object *object = FindObjectByName(ReadAttribute(positionChild, "entityRef"));

		// Check for optional Orientation element
		pugi::xml_node orientation_node = positionChild.child("Orientation");
		OSCOrientation orientation;
		if (orientation_node)
		{
			parseOSCOrientation(orientation, orientation_node);
		}

		OSCPositionRelativeObject *pos = new OSCPositionRelativeObject(object, dx, dy, dz, orientation);

		pos_return = (OSCPosition*)pos;
	}
	else if (positionChildName == "RelativeLanePosition")
	{
		int dLane;
		double ds, offset;

		dLane = strtoi(ReadAttribute(positionChild, "dLane"));
		ds = strtod(ReadAttribute(positionChild, "ds"));
		offset = strtod(ReadAttribute(positionChild, "offset"));
		Object *object = FindObjectByName(ReadAttribute(positionChild, "object"));

		// Check for optional Orientation element
		pugi::xml_node orientation_node = positionChild.child("Orientation");
		OSCOrientation orientation;
		if (orientation_node)
		{
			parseOSCOrientation(orientation, orientation_node);
		}

		OSCPositionRelativeLane *pos = new OSCPositionRelativeLane(object, dLane, ds, offset, orientation);

		pos_return = (OSCPosition*)pos;
	}
	else if (positionChildName == "RoadPosition")
	{
		LOG("%s is not implemented ", positionChildName.c_str());
	}
	else if (positionChildName == "RelativeRoadPosition")
	{
		LOG("%s is not implemented ", positionChildName.c_str());
	}
	else if (positionChildName == "LanePosition")
	{
		int road_id = strtoi(ReadAttribute(positionChild, "roadId"));
		int lane_id = strtoi(ReadAttribute(positionChild, "laneId"));
		double s = strtod(ReadAttribute(positionChild, "s"));

		double offset = 0;  // Default value of optional parameter
		if (positionChild.attribute("offset"))
		{
			offset = strtod(ReadAttribute(positionChild, "offset"));
		}

		// Check for optional Orientation element
		pugi::xml_node orientation_node = positionChild.child("Orientation");
		OSCOrientation orientation;
		if (orientation_node)
		{
			parseOSCOrientation(orientation, orientation_node);
		}

		OSCPositionLane *pos = new OSCPositionLane(road_id, lane_id, s, offset, orientation);

		pos_return = (OSCPosition*)pos;
	}
	else if (positionChildName == "RoutePosition")
	{
		roadmanager::Route *route = 0;
		OSCPositionRoute *pos = new OSCPositionRoute();
		OSCOrientation *orientation = 0;

		for (pugi::xml_node routeChild = positionChild.first_child(); routeChild; routeChild = routeChild.next_sibling())
		{
			if (routeChild.name() == std::string("RouteRef"))
			{
				for (pugi::xml_node routeRefChild = routeChild.first_child(); routeRefChild; routeRefChild = routeRefChild.next_sibling())
				{
					std::string routeRefChildName(routeRefChild.name());

					if (routeRefChildName == "Route")
					{
						// Add inline route to route catalog
						LOG("Inline route reference not supported yet - put the route into a catalog");
					}
					else if (routeRefChildName == "CatalogReference")
					{
						// Find route in catalog
						Entry *entry = ResolveCatalogReference(routeRefChild);

						if (entry == 0)
						{
							return 0;
						}

						if (entry->type_ == CatalogType::CATALOG_ROUTE)
						{
							// Make a new instance from catalog entry 
							route = parseOSCRoute(entry->GetNode());
						}
						else
						{
							LOG("Catalog entry of type %s expected - found %s", Entry::GetTypeAsStr_(CatalogType::CATALOG_ROUTE).c_str(), entry->GetTypeAsStr().c_str());
							return 0;
						}

						RestoreParameterDeclarations();
					}
				}
			} 
			else if (routeChild.name() == std::string("Orientation"))
			{
				orientation = new OSCOrientation;
				parseOSCOrientation(*orientation, routeChild);
			}
			else if (routeChild.name() == std::string("Position"))
			{
				for (pugi::xml_node positionChild = routeChild.first_child(); positionChild; positionChild = positionChild.next_sibling())
				{
					std::string positionChildName(positionChild.name());

					if (positionChildName == "Current")
					{
						LOG("%s is not implemented", positionChildName.c_str());
					}
					else if (positionChildName == "RoadCoord")
					{
						LOG("%s is not implemented", positionChildName.c_str());
					}
					else if (positionChildName == "LaneCoord")
					{
						double s = strtod(ReadAttribute(positionChild, "pathS"));
						int lane_id = strtoi(ReadAttribute(positionChild, "laneId"));
						double lane_offset = 0;

						pugi::xml_attribute laneOffsetAttribute = positionChild.attribute("laneOffset");
						if (laneOffsetAttribute != NULL)
						{
							lane_offset = strtod(ReadAttribute(positionChild, "laneOffset"));
						}
						if (orientation)
						{
							pos->SetRouteRefLaneCoord(route, s, lane_id, lane_offset, orientation);
						}
						else
						{
							pos->SetRouteRefLaneCoord(route, s, lane_id, lane_offset);
							if (lane_id > 0)
							{
								pos->SetRouteRelativeHeading(M_PI);
							}
						}
					}
				}
			}
		}
		pos_return = (OSCPosition*)pos;
	}

	if (pos_return)
	{
		pos_return->Print();
	}
	else
	{
		throw std::runtime_error("Failed parse position");
	}
	return pos_return;
}

OSCPrivateAction::DynamicsShape ParseDynamicsShape(std::string shape)
{
	if (shape == "linear")
	{
		return OSCPrivateAction::DynamicsShape::LINEAR;
	}
	else if (shape == "sinusoidal")
	{
		return OSCPrivateAction::DynamicsShape::SINUSOIDAL;
	}
	else if (shape == "step")
	{
		return OSCPrivateAction::DynamicsShape::STEP;
	}
	else
	{
		LOG("Dynamics shape %s not implemented", shape.c_str());
	}

	return OSCPrivateAction::DynamicsShape::SHAPE_UNDEFINED;
}

OSCPrivateAction::DynamicsDimension ParseDynamicsDimension(std::string dimension)
{
	if (dimension == "rate")
	{
		return OSCPrivateAction::DynamicsDimension::RATE;
	}
	else if (dimension == "time")
	{
		return OSCPrivateAction::DynamicsDimension::TIME;
	}
	else if (dimension == "distance")
	{
		return OSCPrivateAction::DynamicsDimension::DISTANCE;
	}
	else
	{
		LOG("Dynamics dimension %s not supported", dimension.c_str());
	}

	return OSCPrivateAction::DynamicsDimension::DIMENSION_UNDEFINED;
}

int ScenarioReader::ParseTransitionDynamics(pugi::xml_node node, OSCPrivateAction::TransitionDynamics& td)
{
	td.shape_ = ParseDynamicsShape(ReadAttribute(node, "dynamicsShape"));
	td.dimension_ = ParseDynamicsDimension(ReadAttribute(node, "dynamicsDimension"));
	td.target_value_ = strtod(ReadAttribute(node, "value"));

	return 0;
}

OSCGlobalAction *ScenarioReader::parseOSCGlobalAction(pugi::xml_node actionNode)
{
	OSCGlobalAction *action = 0;

	for (pugi::xml_node actionChild = actionNode.first_child(); actionChild; actionChild = actionChild.next_sibling())
	{
		if (actionChild.name() == std::string("EXT_Quit"))
		{
			action = new EXT_QuitAction();
		}
		else
		{
			LOG("Unsupported global action: %s", actionChild.name());
		}
	}

	if (action != 0)
	{
		if (actionNode.parent().attribute("name"))
		{
			action->name_ = ReadAttribute(actionNode.parent(), "name");
		}
		else
		{
			action->name_ = "no name";
		}
	}

	return action;
}

// ------------------------------------------------------
OSCPrivateAction *ScenarioReader::parseOSCPrivateAction(pugi::xml_node actionNode, Object *object)
{
	OSCPrivateAction *action = 0;

	for (pugi::xml_node actionChild = actionNode.first_child(); actionChild; actionChild = actionChild.next_sibling())
	{
		if (actionChild.name() == std::string("LongitudinalAction"))
		{
			for (pugi::xml_node longitudinalChild = actionChild.first_child(); longitudinalChild; longitudinalChild = longitudinalChild.next_sibling())
			{
				if (longitudinalChild.name() == std::string("SpeedAction"))
				{
					LongSpeedAction *action_speed = new LongSpeedAction();

					for (pugi::xml_node speedChild = longitudinalChild.first_child(); speedChild; speedChild = speedChild.next_sibling())
					{
						if (speedChild.name() == std::string("SpeedActionDynamics"))
						{
							ParseTransitionDynamics(speedChild, action_speed->transition_dynamics_);
						}
						else if (speedChild.name() == std::string("SpeedActionTarget"))
						{
							for (pugi::xml_node targetChild = speedChild.first_child(); targetChild; targetChild = targetChild.next_sibling())
							{
								if (targetChild.name() == std::string("RelativeTargetSpeed"))
								{
									LongSpeedAction::TargetRelative *target_rel = new LongSpeedAction::TargetRelative;

									target_rel->value_ = strtod(ReadAttribute(targetChild, "value"));

									target_rel->continuous_ = (
										ReadAttribute(targetChild, "continuous") == "true" ||
										ReadAttribute(targetChild, "continuous") == "1");
									
									target_rel->object_ = FindObjectByName(ReadAttribute(targetChild, "object"));

									std::string value_type = ReadAttribute(targetChild, "valueType");
									if (value_type == "delta")
									{
										target_rel->value_type_ = LongSpeedAction::TargetRelative::ValueType::DELTA;
									}
									else if(value_type == "factor")
									{
										target_rel->value_type_ = LongSpeedAction::TargetRelative::ValueType::FACTOR;
									}
									else if(value_type == "")
									{
										LOG("Value type missing - falling back to delta");
										target_rel->value_type_ = LongSpeedAction::TargetRelative::DELTA;
									}
									else
									{
										LOG("Value type %s not valid", value_type.c_str());
									}
									action_speed->target_ = target_rel;
								}
								else if (targetChild.name() == std::string("AbsoluteTargetSpeed"))
								{
									LongSpeedAction::TargetAbsolute *target_abs = new LongSpeedAction::TargetAbsolute;

									target_abs->value_ = strtod(ReadAttribute(targetChild, "value"));
									action_speed->target_ = target_abs;
								}
								else
								{
									LOG("Unsupported Target type: %s", targetChild.name());
								}
							}
						}
					}
					action = action_speed;
				}
				else if (longitudinalChild.name() == std::string("LongitudinalDistanceAction"))
				{
					LongDistanceAction *action_dist = new LongDistanceAction();

					pugi::xml_node dynamics_node = longitudinalChild.child("DynamicsConstraints");
					if (dynamics_node != NULL)
					{
						pugi::xml_node limits_node = dynamics_node.child("dynamicConstraints");
						if (limits_node)
						{
							action_dist->dynamics_.max_acceleration_ = strtod(ReadAttribute(limits_node, "maxAcceleration"));
							action_dist->dynamics_.max_deceleration_ = strtod(ReadAttribute(limits_node, "maxDeceleration"));
							action_dist->dynamics_.max_speed_ = strtod(ReadAttribute(limits_node, "maxSpeed"));
						}
					}
					else
					{
						action_dist->dynamics_.none_ = true;
					}
					
					action_dist->target_object_ = FindObjectByName(ReadAttribute(longitudinalChild, "entityRef"));
					if (longitudinalChild.attribute("distance"))
					{
						action_dist->dist_type_ = LongDistanceAction::DistType::DISTANCE;
						action_dist->distance_ = strtod(ReadAttribute(longitudinalChild, "distance"));
					}
					else if (longitudinalChild.attribute("timeGap"))
					{
						action_dist->dist_type_ = LongDistanceAction::DistType::TIME_GAP;
						action_dist->distance_ = strtod(ReadAttribute(longitudinalChild, "timeGap"));
					}
					else
					{
						LOG("Need distance or timeGap");
					}
					
					if (longitudinalChild.attribute("continuous"))
					{
						LOG("continuous flag assumed and always on by default");
					}

					std::string freespace = ReadAttribute(longitudinalChild, "freespace");
					if (freespace == "true" || freespace == "1") action_dist->freespace_ = true;
					else action_dist->freespace_ = false;

					action = action_dist;
				}
			}
		}
		else if (actionChild.name() == std::string("LateralAction"))
		{
			for (pugi::xml_node lateralChild = actionChild.first_child(); lateralChild; lateralChild = lateralChild.next_sibling())
			{
				if (lateralChild.name() == std::string("LaneChangeAction"))
				{
					LatLaneChangeAction *action_lane = new LatLaneChangeAction();

					if (ReadAttribute(lateralChild, "targetLaneOffset") != "")
					{
						action_lane->target_lane_offset_ = strtod(ReadAttribute(lateralChild, "targetLaneOffset"));
					}
					else
					{
						action_lane->target_lane_offset_ = 0;
					}

					for (pugi::xml_node laneChangeChild = lateralChild.first_child(); laneChangeChild; laneChangeChild = laneChangeChild.next_sibling())
					{
						if (laneChangeChild.name() == std::string("LaneChangeActionDynamics"))
						{
							ParseTransitionDynamics(laneChangeChild, action_lane->transition_dynamics_);
						}
						else if (laneChangeChild.name() == std::string("Target"))
						{
							LatLaneChangeAction::Target *target;

							for (pugi::xml_node targetChild = laneChangeChild.first_child(); targetChild; targetChild = targetChild.next_sibling())
							{
								if (targetChild.name() == std::string("Relative"))
								{
									LatLaneChangeAction::TargetRelative *target_rel = new LatLaneChangeAction::TargetRelative;

									if ((target_rel->object_ = FindObjectByName(ReadAttribute(targetChild, "object"))) == 0)
									{
										LOG("Failed to find object %s", ReadAttribute(targetChild, "object").c_str());
										return 0;
									}
									target_rel->value_ = strtoi(ReadAttribute(targetChild, "value"));
									target = target_rel;
								}
								else if (targetChild.name() == std::string("Absolute"))
								{
									LatLaneChangeAction::TargetAbsolute *target_abs = new LatLaneChangeAction::TargetAbsolute;

									target_abs->value_ = strtoi(ReadAttribute(targetChild, "value"));
									target = target_abs;
								}
							}
							action_lane->target_ = target;
						}
					}
					action = action_lane;
				}
				else if(lateralChild.name() == std::string("LaneOffsetAction"))
				{
					LatLaneOffsetAction *action_lane = new LatLaneOffsetAction();
					for (pugi::xml_node laneOffsetChild = lateralChild.first_child(); laneOffsetChild; laneOffsetChild = laneOffsetChild.next_sibling())
					{
						if (laneOffsetChild.name() == std::string("Dynamics"))
						{
							if (ReadAttribute(laneOffsetChild, "maxLateralAcc") != "")
							{
								action_lane->dynamics_.max_lateral_acc_= strtod(ReadAttribute(laneOffsetChild, "maxLateralAcc"));
							}

							if (ReadAttribute(laneOffsetChild, "duration") != "")
							{
								action_lane->dynamics_.duration_ = strtod(ReadAttribute(laneOffsetChild, "duration"));
							}

							action_lane->dynamics_.transition_.shape_ = ParseDynamicsShape(ReadAttribute(laneOffsetChild, "shape"));
						}
						else if (laneOffsetChild.name() == std::string("Target"))
						{
							LatLaneOffsetAction::Target *target;

							for (pugi::xml_node targetChild = laneOffsetChild.first_child(); targetChild; targetChild = targetChild.next_sibling())
							{
								if (targetChild.name() == std::string("Relative"))
								{
									LatLaneOffsetAction::TargetRelative *target_rel = new LatLaneOffsetAction::TargetRelative;

									target_rel->object_ = FindObjectByName(ReadAttribute(targetChild, "object"));
									target_rel->value_ = strtod(ReadAttribute(targetChild, "value"));
									target = target_rel;
								}
								else if (targetChild.name() == std::string("Absolute"))
								{
									LatLaneOffsetAction::TargetAbsolute *target_abs = new LatLaneOffsetAction::TargetAbsolute;

									target_abs->value_ = strtod(ReadAttribute(targetChild, "value"));
									target = target_abs;
								}
							}
							action_lane->target_ = target;
						}
					}
					action = action_lane;
				}
				else
				{
					LOG("Unsupported element type: %s", lateralChild.name());
				}
			}
		}
		else if (actionChild.name() == std::string("SynchronizeAction"))
		{
			SynchronizeAction *action_synch = new SynchronizeAction;

			std::string master_object_str = ReadAttribute(actionChild, "masterEntityRef");
			action_synch->master_object_ = FindObjectByName(master_object_str);

			pugi::xml_node target_position_master_node = actionChild.child("TargetPositionMaster");
			if (!target_position_master_node)
			{
				LOG("Missing required element \"TargetPositionMaster\"");
				return 0;
			}
			action_synch->target_position_master_ = parseOSCPosition(target_position_master_node)->GetRMPos();

			pugi::xml_node target_position_node = actionChild.child("TargetPosition");
			if (!target_position_node)
			{
				LOG("Missing required element \"TargetPosition\"");
				return 0;
			}
			action_synch->target_position_ = parseOSCPosition(target_position_node)->GetRMPos();

			pugi::xml_node target_speed_node = actionChild.child("FinalSpeed");
			if (target_speed_node)
			{
				LOG("Parsing optional element \"FinalSpeed\"");

				pugi::xml_node target_speed_element = target_speed_node.child("AbsoluteSpeed");
				if (target_speed_element != NULL)
				{
					LongSpeedAction::TargetAbsolute *targetSpeedAbs = new LongSpeedAction::TargetAbsolute;
					targetSpeedAbs->value_ = strtod(ReadAttribute(target_speed_element, "value"));
					action_synch->final_speed_ = targetSpeedAbs;
				}
				else
				{
					pugi::xml_node target_speed_element = target_speed_node.child("RelativeSpeedToMaster");

					if (target_speed_element != NULL)
					{
						LongSpeedAction::TargetRelative *targetSpeedRel = new LongSpeedAction::TargetRelative;

						targetSpeedRel->value_ = strtod(ReadAttribute(target_speed_element, "value"));

						targetSpeedRel->continuous_ = true;  // Continuous adaption needed

						targetSpeedRel->object_ = action_synch->master_object_;  // Master object is the pivot vehicle 

						std::string value_type = ReadAttribute(target_speed_element, "speedTargetValueType");
						if (value_type == "delta")
						{
							targetSpeedRel->value_type_ = LongSpeedAction::TargetRelative::ValueType::DELTA;
						}
						else if (value_type == "factor")
						{
							targetSpeedRel->value_type_ = LongSpeedAction::TargetRelative::ValueType::FACTOR;
						}
						else if (value_type == "")
						{
							LOG("Value type missing - falling back to delta");
							targetSpeedRel->value_type_ = LongSpeedAction::TargetRelative::DELTA;
						}
						else
						{
							LOG("Value type %s not valid", value_type.c_str());
						}
						action_synch->final_speed_ = targetSpeedRel;
					}
					else
					{
						LOG("Missing speed TargetSpeed sub element \"Absolute\" or \"RelativeMaster\"");
						return 0;
					}
				}
			}
			action = action_synch;
		}
		else if (actionChild.name() == std::string("TeleportAction"))
		{
			PositionAction *action_pos = new PositionAction;
			OSCPosition *pos = parseOSCPosition(actionChild.first_child());
			action_pos->position_ = pos;
			action = action_pos;
		}
		else if (actionChild.name() == std::string("RoutingAction"))
		{
			for (pugi::xml_node routingChild = actionChild.first_child(); routingChild; routingChild = routingChild.next_sibling())
			{
				if (routingChild.name() == std::string("AssignRouteAction"))
				{
					for (pugi::xml_node followRouteChild = routingChild.first_child(); followRouteChild; followRouteChild = followRouteChild.next_sibling())
					{
						if (followRouteChild.name() == std::string("Route"))
						{
							LOG("%s is not implemented", followRouteChild.name());
						}
						else if (followRouteChild.name() == std::string("CatalogReference"))
						{
							FollowRouteAction *action_follow_route = new FollowRouteAction;
							
							// Find route in catalog
							Entry *entry = ResolveCatalogReference(followRouteChild);

							if (entry == 0 || entry->node_ == 0)
							{
								return 0;
							}

							if (entry->type_ == CatalogType::CATALOG_ROUTE)
							{
								// Make a new instance from catalog entry 
								action_follow_route->route_ = parseOSCRoute(entry->GetNode());
								action = action_follow_route;
								break;
							}
							else
							{
								LOG("Catalog entry of type %s expected - found %s", Entry::GetTypeAsStr_(CatalogType::CATALOG_ROUTE).c_str(), entry->GetTypeAsStr().c_str());
								return 0;
							}

							RestoreParameterDeclarations();
						}
					}
				}
			}
		}
		else if (actionChild.name() == std::string("Autonomous"))
		{
			AutonomousAction *autonomous = new AutonomousAction;

			std::string activate_str = ReadAttribute(actionChild, "activate");
			if (activate_str == "true" || activate_str == "1")
			{
				autonomous->activate_ = true;
			}
			else if (activate_str == "false" || activate_str == "0")
			{
				autonomous->activate_ = false;
			}
			else
			{
				LOG("Invalid activation value: %s", activate_str.c_str());
			}

			std::string domain_str = ReadAttribute(actionChild, "domain");
			if (domain_str == "longitudinal")
			{
				autonomous->domain_ = AutonomousAction::DomainType::LONGITUDINAL;
			}
			else if (domain_str == "lateral")
			{
				autonomous->domain_ = AutonomousAction::DomainType::LATERAL;
			}
			else if (domain_str == "both")
			{
				autonomous->domain_ = AutonomousAction::DomainType::BOTH;
			}
			else
			{
				LOG("Invalid domain: %s", domain_str.c_str());
			}

			action = autonomous;
		}
		else
		{
			LOG("%s is not supported", actionChild.name());

		}
	}

	if (action != 0)
	{
		if (actionNode.parent().attribute("name"))
		{
			action->name_ = ReadAttribute(actionNode.parent(), "name");
		}
		else
		{
			action->name_ = "no name";
		}
		action->object_ = object;
	}

	return action;
}

Object* ScenarioReader::FindObjectByName(std::string name)
{
	for (size_t i = 0; i < entities_->object_.size(); i++)
	{
		if (name == entities_->object_[i]->name_)
		{
			return entities_->object_[i];
		}
	}

	LOG("Failed to find object %s", name.c_str());
	throw std::runtime_error(std::string("Failed to find object " + name).c_str());
}


void ScenarioReader::parseInit(Init &init)
{
	LOG("Parsing init");

	pugi::xml_node actionsNode = doc_.child("OpenSCENARIO").child("Storyboard").child("Init").child("Actions");

	for (pugi::xml_node actionsChild = actionsNode.first_child(); actionsChild; actionsChild = actionsChild.next_sibling())
	{

		std::string actionsChildName(actionsChild.name());

		if (actionsChildName == "GlobalAction")
		{
			LOG("%s is not implemented", actionsChildName.c_str());

		}
		else if (actionsChildName == "UserDefined")
		{
			LOG("%s is not implemented", actionsChildName.c_str());
		}
		else if (actionsChildName == "Private")
		{
			Object *entityRef;

			entityRef = FindObjectByName(ReadAttribute(actionsChild, "entityRef"));
			if (entityRef != NULL)
			{
				for (pugi::xml_node privateChild = actionsChild.first_child(); privateChild; privateChild = privateChild.next_sibling())
				{
					// Assume children are PrivateActions
					OSCPrivateAction *action = parseOSCPrivateAction(privateChild, entityRef);
					if (action == 0)
					{
						LOG("Failed to parse private action");
						return;
					}
					action->name_ = "Init " + entityRef->name_ + " " + privateChild.first_child().name();
					init.private_action_.push_back(action);
				}
			}
		}
	}
}


static OSCCondition::ConditionEdge ParseConditionEdge(std::string edge)
{
	if (edge == "rising")
	{
		return OSCCondition::ConditionEdge::RISING;
	}
	else if(edge == "falling")
	{
		return OSCCondition::ConditionEdge::FALLING;
	}
	else if (edge == "risingOrFalling")
	{
		return OSCCondition::ConditionEdge::ANY;
	}
	else
	{
		LOG("Unsupported edge: %s", edge.c_str());
	}

	return OSCCondition::ConditionEdge::UNDEFINED;
}

static Rule ParseRule(std::string rule)
{
	if (rule == "greaterThan")
	{
		return Rule::GREATER_THAN;
	}
	else if (rule == "lessThan")
	{
		return Rule::LESS_THAN;
	}
	else if (rule == "equalTo")
	{
		return Rule::EQUAL_TO;
	}
	else
	{
		LOG("Invalid rule %s", rule.c_str());
	}

	return Rule::UNDEFINED;
}

static TrigByState::StoryElementType ParseElementType(std::string element_type)
{
	if (element_type == "act")
	{
		return TrigByState::StoryElementType::ACT;
	}
	else if (element_type == "action")
	{
		return TrigByState::StoryElementType::ACTION;
	}
	else if (element_type == "scene")
	{
		return TrigByState::StoryElementType::SCENE;
	}
	else if (element_type == "maneuver")
	{
		return TrigByState::StoryElementType::MANEUVER;
	}
	else if (element_type == "event")
	{
		return TrigByState::StoryElementType::EVENT;
	}
	else if (element_type == "action")
	{
		return TrigByState::StoryElementType::ACTION;
	}
	else
	{
		LOG("Invalid element type %s", element_type.c_str());
	}

	return TrigByState::StoryElementType::UNDEFINED;
}
// ------------------------------------------
OSCCondition *ScenarioReader::parseOSCCondition(pugi::xml_node conditionNode)
{
	LOG("Parsing OSCCondition %s", ReadAttribute(conditionNode, "name").c_str());

	OSCCondition *condition;

	for (pugi::xml_node conditionChild = conditionNode.first_child(); conditionChild; conditionChild = conditionChild.next_sibling())
	{
		std::string conditionChildName(conditionChild.name());
		if (conditionChildName == "ByEntityCondition")
		{
			pugi::xml_node entity_condition = conditionChild.child("EntityCondition");
			if (entity_condition != NULL)
			{
				for (pugi::xml_node condition_node = entity_condition.first_child(); condition_node; condition_node = condition_node.next_sibling())
				{
					std::string condition_type(condition_node.name());
					if (condition_type == "TimeHeadwayCondition")
					{
						TrigByTimeHeadway *trigger = new TrigByTimeHeadway;
						trigger->object_ = FindObjectByName(ReadAttribute(condition_node, "entity"));

						std::string along_route_str = ReadAttribute(condition_node, "alongRoute");
						if ((along_route_str == "true") || (along_route_str == "1"))
						{
							trigger->along_route_ = true;
						}
						else
						{
							trigger->along_route_ = false;
						}

						std::string freespace_str = ReadAttribute(condition_node, "freespace");
						if ((freespace_str == "true") || (freespace_str == "1"))
						{
							trigger->freespace_ = true;
						}
						else
						{
							trigger->freespace_ = false;
						}
						trigger->value_ = strtod(ReadAttribute(condition_node, "value"));
						trigger->rule_ = ParseRule(ReadAttribute(condition_node, "rule"));

						condition = trigger;
					}
					else if (condition_type == "ReachPositionCondition")
					{
						TrigByReachPosition *trigger = new TrigByReachPosition;

						if (!condition_node.attribute("tolerance"))
						{
							LOG("tolerance is required");
						}
						else
						{
							trigger->tolerance_ = strtod(ReadAttribute(condition_node, "tolerance"));
						}

						// Read position
						pugi::xml_node pos_node = condition_node.child("Position");
						trigger->position_ = parseOSCPosition(pos_node);

						condition = trigger;
					}
					else if (condition_type == "RelativeDistanceCondition")
					{
						TrigByRelativeDistance *trigger = new TrigByRelativeDistance;
						trigger->object_ = FindObjectByName(ReadAttribute(condition_node, "entity"));

						std::string type = ReadAttribute(condition_node, "type");
						if ((type == "longitudinal") || (type == "Longitudinal"))
						{
							trigger->type_ = TrigByRelativeDistance::RelativeDistanceType::LONGITUDINAL;
						}
						else if ((type == "lateral") || (type == "Lateral"))
						{
							trigger->type_ = TrigByRelativeDistance::RelativeDistanceType::LATERAL;
						}
						else if ((type == "inertial") || (type == "Inertial"))
						{
							trigger->type_ = TrigByRelativeDistance::RelativeDistanceType::INTERIAL;
						}
						else
						{
							LOG("Unknown RelativeDistance condition type: %s", type.c_str());
						}

						std::string freespace_str = ReadAttribute(condition_node, "freespace");
						if ((freespace_str == "true") || (freespace_str == "1"))
						{
							trigger->freespace_ = true;
						}
						else
						{
							trigger->freespace_ = false;
						}
						trigger->value_ = strtod(ReadAttribute(condition_node, "value"));
						trigger->rule_ = ParseRule(ReadAttribute(condition_node, "rule"));

						condition = trigger;
					}
					else if (condition_type == "DistanceCondition")
					{
						TrigByDistance *trigger = new TrigByDistance;

						// Read position
						pugi::xml_node pos_node = condition_node.child("Position");

						trigger->position_ = parseOSCPosition(pos_node);

						std::string freespace_str = ReadAttribute(condition_node, "freespace");
						if ((freespace_str == "true") || (freespace_str == "1"))
						{
							trigger->freespace_ = true;
						}
						else
						{
							trigger->freespace_ = false;
						}

						std::string along_route_str = ReadAttribute(condition_node, "alongRoute");
						if ((along_route_str == "true") || (along_route_str == "1"))
						{
							LOG("Condition Distance along route not supported yet - falling back to alongeRoute = false");
							trigger->along_route_ = false;
						}
						else
						{
							trigger->along_route_ = false;
						}

						trigger->value_ = strtod(ReadAttribute(condition_node, "value"));
						trigger->rule_ = ParseRule(ReadAttribute(condition_node, "rule"));

						condition = trigger;
					}
					else
					{
						LOG("Entity condition %s not supported", condition_type.c_str());
					}
				}
			}

			pugi::xml_node triggering_entities = conditionChild.child("TriggeringEntities");
			if (triggering_entities != NULL)
			{
				TrigByEntity *trigger = (TrigByEntity*)condition;				
				
				std::string trig_ent_rule = ReadAttribute(triggering_entities, "triggeringEntitiesRule");
				if (trig_ent_rule == "any")
				{
					trigger->triggering_entity_rule_ = TrigByEntity::TriggeringEntitiesRule::ANY;
				}
				else if (trig_ent_rule == "all")
				{
					trigger->triggering_entity_rule_ = TrigByEntity::TriggeringEntitiesRule::ALL;
				}
				else
				{
					LOG("Invalid triggering entity type: %s", trig_ent_rule.c_str());
				}

				for (pugi::xml_node triggeringEntitiesChild = triggering_entities.first_child(); triggeringEntitiesChild; triggeringEntitiesChild = triggeringEntitiesChild.next_sibling())
				{
					std::string triggeringEntitiesChildName(triggeringEntitiesChild.name());

					if (triggeringEntitiesChildName == "EntityRef")
					{
						TrigByEntity::Entity entity;
						entity.object_ = FindObjectByName(ReadAttribute(triggeringEntitiesChild, "entityRef"));
						trigger->triggering_entities_.entity_.push_back(entity);
					}
				}
			}
		}
		else if (conditionChildName == "ByStateCondition")
		{
			for (pugi::xml_node byStateChild = conditionChild.first_child(); byStateChild; byStateChild = byStateChild.next_sibling())
			{
				std::string byStateChildName(byStateChild.name());

				if (byStateChildName == "AtStart")
				{
					TrigAtStart *trigger = new TrigAtStart;
					trigger->element_type_ = ParseElementType(ReadAttribute(byStateChild, "type"));
					trigger->element_name_ = ReadAttribute(byStateChild, "name");
					condition = trigger;
				}
				else if (byStateChildName == "AfterTermination")
				{
					TrigAfterTermination *trigger = new TrigAfterTermination;
					trigger->element_name_ = ReadAttribute(byStateChild, "name");
					trigger->element_type_ = ParseElementType(ReadAttribute(byStateChild, "type"));
					std::string term_rule = ReadAttribute(byStateChild, "rule");
					if (term_rule == "end")
					{
						trigger->rule_ = TrigAfterTermination::AfterTerminationRule::END;
					} 
					else if (term_rule == "cancel")
					{
						trigger->rule_ = TrigAfterTermination::AfterTerminationRule::CANCEL;
					}
					else if (term_rule == "any")
					{
						trigger->rule_ = TrigAfterTermination::AfterTerminationRule::ANY;
					}
					else
					{
						LOG("Invalid AfterTerminationRule %s", term_rule.c_str());
					}					
					condition = trigger;
				}
				else 
				{
					LOG("%s is not implemented", byStateChildName.c_str());
				}
			}
		}
		else if (conditionChildName == "ByValueCondition")
		{
			for (pugi::xml_node byValueChild = conditionChild.first_child(); byValueChild; byValueChild = byValueChild.next_sibling())
			{
				std::string byValueChildName(byValueChild.name());
				if (byValueChildName == "SimulationTimeCondition")
				{
					TrigBySimulationTime *trigger = new TrigBySimulationTime;
					trigger->value_ = strtod(ReadAttribute(byValueChild, "value"));
					trigger->rule_ = ParseRule(ReadAttribute(byValueChild, "rule"));
					condition = trigger;
				}
				else
				{
					LOG("TrigByValue %s not implemented", byValueChildName.c_str());
				}
			}
		}
		else
		{
			LOG("Condition %s not supported\n", conditionChildName.c_str());
		}
	}
	condition->name_ = ReadAttribute(conditionNode, "name");
	if (conditionNode.attribute("delay") != NULL)
	{
		condition->delay_ = strtod(ReadAttribute(conditionNode, "delay"));
	}
	else
	{
		LOG("Attribute \"delay\" missing");
	}

	std::string edge_str = ReadAttribute(conditionNode, "conditionEdge");
	if (edge_str != "")
	{
		condition->edge_ = ParseConditionEdge(edge_str);
	}

	return condition;
}


void ScenarioReader::parseOSCManeuver(OSCManeuver *maneuver, pugi::xml_node maneuverNode, ManeuverGroup *mGroup)
{
	maneuver->name_ = ReadAttribute(maneuverNode, "name");
	LOG("Parsing OSCManeuver %s", maneuver->name_.c_str());

	for (pugi::xml_node maneuverChild = maneuverNode.first_child(); maneuverChild; maneuverChild = maneuverChild.next_sibling())
	{
		std::string maneuverChildName(maneuverChild.name());

		if (maneuverChildName == "ParameterDeclarations")
		{
			addParameterDeclarations(maneuverChild);
		}
		else if (maneuverChildName == "Event")
		{
			Event *event = new Event;

			event->name_ = ReadAttribute(maneuverChild, "name");
			LOG("Parsing Event %s", event->name_.c_str());

			std::string prio = ReadAttribute(maneuverChild, "priority");
			if (prio == "overwrite")
			{
				event->priority_ = Event::Priority::OVERWRITE;
			}
			else if (prio == "following")
			{
				event->priority_ = Event::Priority::FOLLOWING;
			}
			else if (prio == "skip")
			{
				event->priority_ = Event::Priority::SKIP;
			}
			else
			{
				LOG("Invalid priority: %s", prio.c_str());
			}

			for (pugi::xml_node eventChild = maneuverChild.first_child(); eventChild; eventChild = eventChild.next_sibling())
			{

				std::string childName(eventChild.name());

				if (childName == "Action")
				{
					for (pugi::xml_node actionChild = eventChild.first_child(); actionChild; actionChild = actionChild.next_sibling())
					{
						std::string childName(actionChild.name());

						if (childName == "GlobalAction")
						{
							LOG("Parsing global action %s", ReadAttribute(eventChild, "name").c_str());
							OSCGlobalAction *action = parseOSCGlobalAction(actionChild);
							event->action_.push_back((OSCAction*)action);
						}
						else if (childName == "UserDefinedAction")
						{
							LOG("%s is not implemented", childName.c_str());
						}
						else if (childName == "PrivateAction")
						{
							for (size_t i = 0; i < mGroup->actor_.size(); i++)
							{
								LOG("Parsing private action %s", ReadAttribute(eventChild, "name").c_str());
								OSCPrivateAction *action = parseOSCPrivateAction(actionChild, mGroup->actor_[i]->object_);
								event->action_.push_back((OSCAction*)action);
							}
						}
					}
				}
				else if (childName == "StartTrigger")
				{
					for (pugi::xml_node startConditionsChild = eventChild.first_child(); startConditionsChild; startConditionsChild = startConditionsChild.next_sibling())
					{
						OSCConditionGroup *condition_group = new OSCConditionGroup;

						for (pugi::xml_node conditionGroupChild = startConditionsChild.first_child(); conditionGroupChild; conditionGroupChild = conditionGroupChild.next_sibling())
						{
							OSCCondition *condition = parseOSCCondition(conditionGroupChild);
							condition_group->condition_.push_back(condition);
						}

						event->start_condition_group_.push_back(condition_group);
					}
				}
				else
				{
					LOG("%s not supported", childName.c_str());
				}
			}
			maneuver->event_.push_back(event);
		}
	}
}

OSCConditionGroup* ScenarioReader::ParseConditionGroup(pugi::xml_node node)
{
	OSCConditionGroup* cg = new OSCConditionGroup();

	for (pugi::xml_node conditionNode = node.first_child(); conditionNode; conditionNode = conditionNode.next_sibling())
	{
		OSCCondition* condition = parseOSCCondition(conditionNode);
		cg->condition_.push_back(condition);
	}

	return cg;
}

int ScenarioReader::parseStoryBoard(StoryBoard &storyBoard)
{
	LOG("Parsing Story");

	pugi::xml_node storyNode = doc_.child("OpenSCENARIO").child("Storyboard").child("Story");

	for (; storyNode; storyNode = storyNode.next_sibling())
	{
		std::string storyNodeName(storyNode.name());

		if (storyNodeName == "Story")
		{
			std::string name = ReadAttribute(storyNode, "name", true);
			Story *story = new Story(name);

			if (!strcmp(storyNode.first_child().name(), "ParameterDeclarations"))
			{
				addParameterDeclarations(storyNode.first_child());
			}

			for (pugi::xml_node storyChild = storyNode.child("Act"); storyChild; storyChild = storyChild.next_sibling("Act"))
			{
				Act *act = new Act;

				act->name_ = ReadAttribute(storyChild, "name");

				for (pugi::xml_node actChild = storyChild.first_child(); actChild; actChild = actChild.next_sibling())
				{

					std::string childName(actChild.name());

					if (childName == "ManeuverGroup")
					{
						ManeuverGroup *mGroup = new ManeuverGroup;

						mGroup->number_of_executions_ = strtoi(ReadAttribute(actChild, "numberOfExecutions"));
						mGroup->name_ = ReadAttribute(actChild, "name");

						pugi::xml_node actors_node = actChild.child("Actors");
						if (actors_node != NULL)
						{
							for (pugi::xml_node actorsChild = actors_node.first_child(); actorsChild; actorsChild = actorsChild.next_sibling())
							{
								ManeuverGroup::Actor *actor = new ManeuverGroup::Actor;

								std::string actorsChildName(actorsChild.name());
								if (actorsChildName == "EntityRef")
								{
									actor->object_ = FindObjectByName(ReadAttribute(actorsChild, "entityRef"));
								}
								else if (actorsChildName == "ByCondition")
								{
									LOG("Actor by condition - not implemented");
								}
								mGroup->actor_.push_back(actor);
								if (actor->object_->ghost_)
								{
									// Add ghost as well
									ManeuverGroup::Actor *actor = new ManeuverGroup::Actor;
									actor->object_ = FindObjectByName(ReadAttribute(actorsChild, "name").append("_ghost"));
									mGroup->actor_.push_back(actor);
								}
							}
						}

						for (pugi::xml_node catalog_n = actChild.child("CatalogReference"); catalog_n; catalog_n = catalog_n.next_sibling("CatalogReference"))
						{
							// Maneuver catalog reference. The catalog entry is simply the maneuver XML node
							Entry *entry = ResolveCatalogReference(catalog_n);

							if (entry == 0 || entry->node_ == 0)
							{
								return -1;
							}

							if (entry->type_ == CatalogType::CATALOG_MANEUVER)
							{
								OSCManeuver *maneuver = new OSCManeuver;

								// Make a new instance from catalog entry 
								parseOSCManeuver(maneuver, entry->GetNode(), mGroup);
								mGroup->maneuver_.push_back(maneuver);
							}
							else
							{
								LOG("Unexpected catalog type %s", entry->GetTypeAsStr().c_str());
							}

							// Remove temporary parameters used for catalog reference
							RestoreParameterDeclarations();
						}

						for (pugi::xml_node maneuver_n = actChild.child("Maneuver"); maneuver_n; maneuver_n = maneuver_n.next_sibling("Maneuver"))
						if (maneuver_n != NULL)
						{
							OSCManeuver *maneuver = new OSCManeuver;

							parseOSCManeuver(maneuver, maneuver_n, mGroup);
							mGroup->maneuver_.push_back(maneuver);
						}

						act->sequence_.push_back(mGroup);
					}
					else if (childName == "StartTrigger")
					{
						pugi::xml_node conditionsGroup = actChild.first_child();
						OSCConditionGroup* condition_group = ParseConditionGroup(conditionsGroup);
						act->start_condition_group_.push_back(condition_group);
					}
					else if (childName == "EndTrigger")
					{
						pugi::xml_node conditionsGroup = actChild.first_child();
						OSCConditionGroup* condition_group = ParseConditionGroup(conditionsGroup);
						act->stop_condition_group_.push_back(condition_group);
					}
				}
				story->act_.push_back(act);
			}
			storyBoard.story_.push_back(story);
			RestoreParameterDeclarations();
		}
	}

	return 0;
}


