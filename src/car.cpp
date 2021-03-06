/************************************************************************/
/*                                                                      */
/* This file is part of VDrift.                                         */
/*                                                                      */
/* VDrift is free software: you can redistribute it and/or modify       */
/* it under the terms of the GNU General Public License as published by */
/* the Free Software Foundation, either version 3 of the License, or    */
/* (at your option) any later version.                                  */
/*                                                                      */
/* VDrift is distributed in the hope that it will be useful,            */
/* but WITHOUT ANY WARRANTY; without even the implied warranty of       */
/* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the        */
/* GNU General Public License for more details.                         */
/*                                                                      */
/* You should have received a copy of the GNU General Public License    */
/* along with VDrift.  If not, see <http://www.gnu.org/licenses/>.      */
/*                                                                      */
/************************************************************************/

#include "car.h"
#include "carinput.h"
#include "content/contentmanager.h"
#include "physics/carwheelposition.h"
#include "physics/dynamicsworld.h"
#include "physics/tracksurface.h"
#include "graphics/textureinfo.h"
#include "graphics/mesh_gen.h"
#include "graphics/model_obj.h"
#include "sound/sound.h"
#include "cfg/ptree.h"
#include "loaddrawable.h"
#include "loadcamera.h"
#include "camera.h"

#include <map>
#include <list>
#include <vector>
#include <sstream>
#include <string>

template <typename T>
static inline T clamp(T val, T min, T max)
{
	return (val < max) ? (val > min) ? val : min : max;
}

enum WHICHDRAWLIST
{
	BLEND,
	NOBLEND,
	EMISSIVE,
	OMNI
};

static keyed_container <DRAWABLE> & GetDrawlist(SCENENODE & node, WHICHDRAWLIST which)
{
	switch (which)
	{
		case BLEND:
		return node.GetDrawlist().normal_blend;

		case NOBLEND:
		return node.GetDrawlist().car_noblend;

		case EMISSIVE:
		return node.GetDrawlist().lights_emissive;

		case OMNI:
		return node.GetDrawlist().lights_omni;
	};
	assert(0);
	return node.GetDrawlist().car_noblend;
}

struct LoadBody
{
	SCENENODE & topnode;
	keyed_container<SCENENODE>::handle & bodynode;
	LoadDrawable & loadDrawable;

	LoadBody(
		SCENENODE & topnode,
		keyed_container<SCENENODE>::handle & bodynode,
		LoadDrawable & loadDrawable) :
		topnode(topnode),
		bodynode(bodynode),
		loadDrawable(loadDrawable)
	{
		// ctor
	}

	bool operator()(const PTree & cfg)
	{
		const PTree * link;
		if (cfg.get("link", link))
		{
			// load breakable body drawables
			if (!loadDrawable(cfg, topnode)) return false;
		}
		else
		{
			// load fixed body drawables
			if (!loadDrawable(cfg, topnode.GetNode(bodynode))) return false;
		}
		return true;
	}
};

static bool LoadWheel(
	const PTree & cfg_wheel,
	struct LoadDrawable & loadDrawable,
	SCENENODE & topnode,
	std::ostream & error_output)
{
	keyed_container<SCENENODE>::handle wheelnode = topnode.AddNode();
	ContentManager & content = loadDrawable.content;
	const std::string& path = loadDrawable.path;

	std::string meshname;
	std::vector<std::string> texname;
	std::tr1::shared_ptr<MODEL> mesh;
	const PTree * cfg_tire;
	MATHVECTOR<float, 3> size(0);
	std::string sizestr;

	if (!cfg_wheel.get("mesh", meshname, error_output)) return false;
	if (!cfg_wheel.get("texture", texname, error_output)) return false;
	if (!cfg_wheel.get("tire", cfg_tire, error_output)) return false;
	if (!cfg_tire->get("size", sizestr, error_output)) return false;
	if (!cfg_tire->get("size", size, error_output)) return false;

	// load wheel
	bool genrim = true;
	cfg_wheel.get("genrim", genrim);
	if (genrim)
	{
		// get wheel disk mesh
		content.load(mesh, path, meshname);

		// gen wheel mesh
		meshname = meshname + sizestr;
		if (!content.get(mesh, path, meshname))
		{
			float width = size[0] * 0.001;
			float diameter = size[2] * 0.0254;

			VERTEXARRAY rimva, diskva;
			MESHGEN::mg_rim(rimva, size[0], size[1], size[2], 10);
			diskva = mesh->GetVertexArray();
			diskva.Translate(-0.75 * 0.5, 0, 0);
			diskva.Scale(width, diameter, diameter);
			content.load(mesh, path, meshname, rimva + diskva);

			//MODEL_OBJ mo("wheel.obj", error_output);
			//mo.SetVertexArray(mesh->GetVertexArray());
			//mo.Save("wheel.obj", error_output);
		}
	}

	if (!loadDrawable(meshname, texname, cfg_wheel, topnode, &wheelnode))
	{
		return false;
	}

	// load tire (optional)
	texname.clear();
	if (cfg_tire->get("texture", texname))
	{
		meshname.clear();
		if (!cfg_tire->get("mesh", meshname))
		{
			// gen tire mesh
			meshname = "tire" + sizestr;
			if (!content.get(mesh, path, meshname))
			{
				VERTEXARRAY tireva;
				MESHGEN::mg_tire(tireva, size[0], size[1], size[2]);
				content.load(mesh, path, meshname, tireva);

				//MODEL_OBJ mo("wheel.obj", error_output);
				//mo.SetVertexArray(mesh->GetVertexArray());
				//mo.Save("tire.obj", error_output);
			}
		}

		if (!loadDrawable(meshname, texname, *cfg_tire, topnode.GetNode(wheelnode)))
		{
			return false;
		}
	}

	// load brake (optional)
	texname.clear();
	const PTree * cfg_brake;
	if (cfg_wheel.get("brake", cfg_brake, error_output) &&
		cfg_brake->get("texture", texname))
	{
		float radius;
		std::string radiusstr;
		cfg_brake->get("radius", radius);
		cfg_brake->get("radius", radiusstr);

		meshname.clear();
		if (!cfg_brake->get("mesh", meshname))
		{
			// gen brake disk mesh
			meshname = "brake" + radiusstr;
			if (!content.get(mesh, path, meshname))
			{
				float diameter_mm = radius * 2 * 1000;
				float thickness_mm = 0.025 * 1000;
				VERTEXARRAY brakeva;
				MESHGEN::mg_brake_rotor(brakeva, diameter_mm, thickness_mm);
				content.load(mesh, path, meshname, brakeva);
			}
		}

		if (!loadDrawable(meshname, texname, *cfg_brake, topnode.GetNode(wheelnode)))
		{
			return false;
		}
	}

	return true;
}

CAR::CAR() :
	psound(0),
	gearsound_check(0),
	brakesound_check(false),
	handbrakesound_check(false),
	steer_angle_max(0),
	last_steer(0),
	nos_active(false),
	driver_view(false),
	sector(-1),
	applied_brakes(0)
{
	// ctor
}

CAR::~CAR()
{
	RemoveSounds();

	for (size_t i = 0; i < cameras.size(); ++i)
		delete cameras[i];
}

bool CAR::LoadLight(
	const PTree & cfg,
	ContentManager & content,
	std::ostream & error_output)
{
	float radius;
	std::string radiusstr;
	MATHVECTOR<float, 3> pos(0), col(0);
	if (!cfg.get("position", pos, error_output)) return false;
	if (!cfg.get("color", col, error_output)) return false;
	if (!cfg.get("radius", radius, error_output)) return false;
	cfg.get("radius", radiusstr);

	lights.push_back(LIGHT());

	SCENENODE & bodynoderef = topnode.GetNode(bodynode);
	lights.back().node = bodynoderef.AddNode();

	SCENENODE & node = bodynoderef.GetNode(lights.back().node);
	node.GetTransform().SetTranslation(MATHVECTOR<float,3>(pos[0], pos[1], pos[2]));

	std::tr1::shared_ptr<MODEL> mesh;
	if (!content.get(mesh, "", "cube" + radiusstr))
	{
		VERTEXARRAY varray;
		varray.SetToUnitCube();
		varray.Scale(radius, radius, radius);
		content.load(mesh, "", "cube" + radiusstr, varray);
	}
    models.push_back(mesh);

	keyed_container <DRAWABLE> & dlist = GetDrawlist(node, OMNI);
	lights.back().draw = dlist.insert(DRAWABLE());

	DRAWABLE & draw = dlist.get(lights.back().draw);
	draw.SetColor(col[0], col[1], col[2]);
	draw.SetModel(*mesh);
	draw.SetCull(true, true);
	draw.SetDrawEnable(false);

	return true;
}

bool CAR::LoadGraphics(
	const PTree & cfg,
	const std::string & carpath,
	const std::string & carname,
	const std::string & carwheel,
	const std::string & carpaint,
	const MATHVECTOR <float, 3> & carcolor,
	const int anisotropy,
	const float camerabounce,
	ContentManager & content,
	std::ostream & error_output)
{
	//write_inf(cfg, std::cerr);
	cartype = carname;

	// init drawable load functor
	LoadDrawable loadDrawable(carpath, anisotropy, content, models, error_output);

	// load body first
	const PTree * cfg_body;
	std::string meshname;
	std::vector<std::string> texname;
	if (!cfg.get("body", cfg_body, error_output)) return false;
	if (!cfg_body->get("mesh", meshname, error_output)) return false;
	if (!cfg_body->get("texture", texname, error_output)) return false;
	if (carpaint != "default") texname[0] = carpaint;
	if (!loadDrawable(meshname, texname, *cfg_body, topnode, &bodynode)) return false;

	// load wheels
	const PTree * cfg_wheels;
	if (!cfg.get("wheel", cfg_wheels, error_output)) return false;

	std::tr1::shared_ptr<PTree> sel_wheel;
	if (carwheel != "default" && !content.load(sel_wheel, carpath, carwheel)) return false;

	for (PTree::const_iterator i = cfg_wheels->begin(); i != cfg_wheels->end(); ++i)
	{
		const PTree * cfg_wheel = &i->second;

		// override default wheel with selected, not very efficient, fixme
		PTree opt_wheel;
		if (sel_wheel.get())
		{
			opt_wheel.set(*sel_wheel);
			opt_wheel.merge(*cfg_wheel);
			cfg_wheel = &opt_wheel;
		}

		if (!LoadWheel(*cfg_wheel, loadDrawable, topnode, error_output))
		{
			error_output << "Failed to load wheels." << std::endl;
			return false;
		}
	}

	// load drawables
	LoadBody loadBody(topnode, bodynode, loadDrawable);
	for (PTree::const_iterator i = cfg.begin(); i != cfg.end(); ++i)
	{
		if (i->first != "body" &&
			i->first != "steering" &&
			i->first != "light-brake" &&
			i->first != "light-reverse")
		{
			loadBody(i->second);
		}
	}

	// load steering wheel
	const PTree * cfg_steer;
	if (cfg.get("steering", cfg_steer))
	{
		SCENENODE & bodynoderef = topnode.GetNode(bodynode);
		if (!loadDrawable(*cfg_steer, bodynoderef, &steernode, 0))
		{
			error_output << "Failed to load steering wheel." << std::endl;
			return false;
		}
		cfg_steer->get("max-angle", steer_angle_max);
		steer_angle_max = steer_angle_max / 180.0 * M_PI;
		SCENENODE & steernoderef = bodynoderef.GetNode(steernode);
		steer_orientation = steernoderef.GetTransform().GetRotation();
	}

	// load brake/reverse light point light sources (optional)
	int i = 0;
	std::string istr = "0";
	const PTree * cfg_light;
	while (cfg.get("light-brake-"+istr, cfg_light))
	{
		if (!LoadLight(*cfg_light, content, error_output))
		{
			error_output << "Failed to load lights." << std::endl;
			return false;
		}

		std::stringstream sstr;
		sstr << ++i;
		istr = sstr.str();
	}
	i = 0;
	istr = "0";
	while (cfg.get("light-reverse-"+istr, cfg_light))
	{
		if (!LoadLight(*cfg_light, content, error_output))
		{
			error_output << "Failed to load lights." << std::endl;
			return false;
		}

		std::stringstream sstr;
		sstr << ++i;
		istr = sstr.str();
	}

	// load car brake/reverse graphics (optional)
	if (cfg.get("light-brake", cfg_light))
	{
		SCENENODE & bodynoderef = topnode.GetNode(bodynode);
		if (!loadDrawable(*cfg_light, bodynoderef, 0, &brakelights))
		{
			error_output << "Failed to load lights." << std::endl;
			return false;
		}
	}
	if (cfg.get("light-reverse", cfg_light))
	{
		SCENENODE & bodynoderef = topnode.GetNode(bodynode);
		if (!loadDrawable(*cfg_light, bodynoderef, 0, &reverselights))
		{
			error_output << "Failed to load lights." << std::endl;
			return false;
		}
	}

	const PTree * cfg_cams;
	if (!cfg.get("camera", cfg_cams))
	{
		return false;
	}
	if (!cfg_cams->size())
	{
		error_output << "No cameras defined." << std::endl;
		return false;
	}
	cameras.reserve(cfg_cams->size());
	for (PTree::const_iterator i = cfg_cams->begin(); i != cfg_cams->end(); ++i)
	{
		CAMERA * cam = LoadCamera(i->second, camerabounce, error_output);
		if (!cam) return false;
		cameras.push_back(cam);
	}

	SetColor(carcolor[0], carcolor[1], carcolor[2]);

	return true;
}

bool CAR::LoadPhysics(
	std::ostream & error_output,
	ContentManager & content,
	DynamicsWorld & world,
	const PTree & cfg,
	const std::string & carpath,
	const std::string & cartire,
	const MATHVECTOR <float, 3> & initial_position,
	const QUATERNION <float> & initial_orientation,
	const bool defaultabs,
	const bool defaulttcs,
	const bool damage)
{
	std::string carmodel;
	if (!cfg.get("body.mesh", carmodel, error_output))
		return false;

	std::tr1::shared_ptr<MODEL> model;
	content.load(model, carpath, carmodel);

	btVector3 size = ToBulletVector(model->GetSize());
	btVector3 center = ToBulletVector(model->GetCenter());
	btVector3 position = ToBulletVector(initial_position);
	btQuaternion rotation = ToBulletQuaternion(initial_orientation);

	if (!dynamics.Load(
		error_output, content, world,
		cfg, carpath, cartire,
		size, center, position, rotation,
		damage))
	{
		return false;
	}

	dynamics.SetABS(defaultabs);
	dynamics.SetTCS(defaulttcs);

	mz_nominalmax = 0.05f * 9.81f / dynamics.GetInvMass(); // fixme: make this a steering feedback parameter

	return true;
}

bool CAR::LoadSounds(
	const std::string & carpath,
	const std::string & carname,
	SOUND & sound,
	ContentManager & content,
	std::ostream & error_output)
{
	psound = &sound;

	// check for sound specification file
	std::string path_aud = carpath + "/" + carname + ".aud";
	std::ifstream file_aud(path_aud.c_str());
	if (file_aud.good())
	{
		PTree aud;
		read_ini(file_aud, aud);
		enginesounds.reserve(aud.size());
		for (PTree::const_iterator i = aud.begin(); i != aud.end(); ++i)
		{
			const PTree & audi = i->second;

			std::string filename;
			std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
			if (!audi.get("filename", filename, error_output)) return false;

			enginesounds.push_back(ENGINESOUNDINFO());
			ENGINESOUNDINFO & info = enginesounds.back();

			if (!audi.get("MinimumRPM", info.minrpm, error_output)) return false;
			if (!audi.get("MaximumRPM", info.maxrpm, error_output)) return false;
			if (!audi.get("NaturalRPM", info.naturalrpm, error_output)) return false;

			bool powersetting;
			if (!audi.get("power", powersetting, error_output)) return false;
			if (powersetting)
				info.power = ENGINESOUNDINFO::POWERON;
			else if (!powersetting)
				info.power = ENGINESOUNDINFO::POWEROFF;
			else
				info.power = ENGINESOUNDINFO::BOTH;

			info.sound_source = sound.AddSource(soundptr, 0, true, true);
			sound.SetSourceGain(info.sound_source, 0);
		}

		// set blend start and end locations -- requires multiple passes
		std::map <ENGINESOUNDINFO *, ENGINESOUNDINFO *> temporary_to_actual_map;
		std::list <ENGINESOUNDINFO> poweron_sounds, poweroff_sounds;
		for (std::vector <ENGINESOUNDINFO>::iterator i = enginesounds.begin(); i != enginesounds.end(); ++i)
		{
			if (i->power == ENGINESOUNDINFO::POWERON)
			{
				poweron_sounds.push_back(*i);
				temporary_to_actual_map[&poweron_sounds.back()] = &*i;
			}
			else if (i->power == ENGINESOUNDINFO::POWEROFF)
			{
				poweroff_sounds.push_back(*i);
				temporary_to_actual_map[&poweroff_sounds.back()] = &*i;
			}
		}

		poweron_sounds.sort();
		poweroff_sounds.sort();

		// we only support 2 overlapping sounds at once each for poweron and poweroff; this
		// algorithm fails for other cases (undefined behavior)
		std::list <ENGINESOUNDINFO> * cursounds = &poweron_sounds;
		for (int n = 0; n < 2; n++)
		{
			if (n == 1)
				cursounds = &poweroff_sounds;

			for (std::list <ENGINESOUNDINFO>::iterator i = (*cursounds).begin(); i != (*cursounds).end(); ++i)
			{
				// set start blend
				if (i == (*cursounds).begin())
					i->fullgainrpmstart = i->minrpm;

				// set end blend
				std::list <ENGINESOUNDINFO>::iterator inext = i;
				inext++;
				if (inext == (*cursounds).end())
					i->fullgainrpmend = i->maxrpm;
				else
				{
					i->fullgainrpmend = inext->minrpm;
					inext->fullgainrpmstart = i->maxrpm;
				}
			}

			// now assign back to the actual infos
			for (std::list <ENGINESOUNDINFO>::iterator i = (*cursounds).begin(); i != (*cursounds).end(); ++i)
			{
				assert(temporary_to_actual_map.find(&(*i)) != temporary_to_actual_map.end());
				*temporary_to_actual_map[&(*i)] = *i;
			}
		}
	}
	else
	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		content.load(soundptr, carpath, "engine");
		enginesounds.push_back(ENGINESOUNDINFO());
		enginesounds.back().sound_source = sound.AddSource(soundptr, 0, true, true);
	}

	//set up tire squeal sounds
	for (int i = 0; i < 4; ++i)
	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		content.load(soundptr, carpath, "tire_squeal");
		tiresqueal[i] = sound.AddSource(soundptr, i * 0.25, true, true);
	}

	//set up tire gravel sounds
	for (int i = 0; i < 4; ++i)
	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		content.load(soundptr, carpath, "gravel");
		gravelsound[i] = sound.AddSource(soundptr, i * 0.25, true, true);
	}

	//set up tire grass sounds
	for (int i = 0; i < 4; ++i)
	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		content.load(soundptr, carpath, "grass");
		grasssound[i] = sound.AddSource(soundptr, i * 0.25, true, true);
	}

	//set up bump sounds
	for (int i = 0; i < 4; ++i)
	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		if (i >= 2)
		{
			content.load(soundptr, carpath, "bump_rear");
		}
		else
		{
			content.load(soundptr, carpath, "bump_front");
		}
		tirebump[i] = sound.AddSource(soundptr, 0, true, false);
	}

	//set up crash sound
	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		content.load(soundptr, carpath, "crash");
		crashsound = sound.AddSource(soundptr, 0, true, false);
	}

	//set up gear sound
	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		content.load(soundptr, carpath, "gear");
		gearsound = sound.AddSource(soundptr, 0, true, false);
	}

	//set up brake sound
	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		content.load(soundptr, carpath, "brake");
		brakesound = sound.AddSource(soundptr, 0, true, false);
	}

	//set up handbrake sound
	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		content.load(soundptr, carpath, "handbrake");
		handbrakesound = sound.AddSource(soundptr, 0, true, false);
	}

	{
		std::tr1::shared_ptr<SOUNDBUFFER> soundptr;
		content.load(soundptr, carpath, "wind");
		roadnoise = sound.AddSource(soundptr, 0, true, true);
	}

	return true;
}

void CAR::SetColor(float r, float g, float b)
{
	SCENENODE & bodynoderef = topnode.GetNode(bodynode);
	keyed_container<DRAWABLE> & car_noblend = bodynoderef.GetDrawlist().car_noblend;
	for (keyed_container<DRAWABLE>::iterator i = car_noblend.begin(); i != car_noblend.end(); ++i)
	{
		i->SetColor(r, g, b, 1);
	}
}

void CAR::SetPosition(const MATHVECTOR <float, 3> & new_position)
{
	btVector3 newpos = ToBulletVector(new_position);
	dynamics.SetPosition(newpos);
	dynamics.AlignWithGround();
}

void CAR::UpdateGraphics()
{
	if (!bodynode.valid()) return;
	assert(dynamics.GetNumBodies() == topnode.Nodes());

	unsigned int i = 0;
	keyed_container<SCENENODE> & childlist = topnode.GetNodelist();
	for (keyed_container<SCENENODE>::iterator ni = childlist.begin(); ni != childlist.end(); ++ni, ++i)
	{
		MATHVECTOR<float, 3> pos = ToMathVector<float>(dynamics.GetPosition(i));
		QUATERNION<float> rot = ToMathQuaternion<float>(dynamics.GetOrientation(i));
		ni->GetTransform().SetTranslation(pos);
		ni->GetTransform().SetRotation(rot);
	}

	// brake/reverse lights
	SCENENODE & bodynoderef = topnode.GetNode(bodynode);
	for (std::list<LIGHT>::iterator i = lights.begin(); i != lights.end(); i++)
	{
		SCENENODE & node = bodynoderef.GetNode(i->node);
		DRAWABLE & draw = GetDrawlist(node, OMNI).get(i->draw);
		draw.SetDrawEnable(applied_brakes > 0);
	}
	if (brakelights.valid())
	{
		GetDrawlist(bodynoderef, EMISSIVE).get(brakelights).SetDrawEnable(applied_brakes > 0);
	}
	if (reverselights.valid())
	{
		GetDrawlist(bodynoderef, EMISSIVE).get(reverselights).SetDrawEnable(GetGear() < 0);
	}

	// steering
	if (steernode.valid())
	{
		SCENENODE & steernoderef = bodynoderef.GetNode(steernode);
		steernoderef.GetTransform().SetRotation(steer_rotation);
	}
}

void CAR::RemoveSounds()
{
	if (!psound) return;

	// reverse order
	psound->RemoveSource(roadnoise);
	psound->RemoveSource(handbrakesound);
	psound->RemoveSource(brakesound);
	psound->RemoveSource(gearsound);
	psound->RemoveSource(crashsound);

	for (int i = WHEEL_POSITION_SIZE - 1; i >= 0; --i)
		psound->RemoveSource(tirebump[i]);

	for (int i = WHEEL_POSITION_SIZE - 1; i >= 0; --i)
		psound->RemoveSource(grasssound[i]);

	for (int i = WHEEL_POSITION_SIZE - 1; i >= 0; --i)
		psound->RemoveSource(gravelsound[i]);

	for (int i = WHEEL_POSITION_SIZE - 1; i >= 0; --i)
		psound->RemoveSource(tiresqueal[i]);

	for (int i = enginesounds.size() - 1; i >= 0; --i)
		psound->RemoveSource(enginesounds[i].sound_source);
}

void CAR::UpdateSounds(float dt)
{
	if (!psound) return;

	MATHVECTOR <float, 3> pos_car = GetPosition();
	MATHVECTOR <float, 3> pos_eng = ToMathVector<float>(dynamics.GetEnginePosition());

	psound->SetSourcePosition(roadnoise, pos_car[0], pos_car[1], pos_car[2]);
	psound->SetSourcePosition(crashsound, pos_car[0], pos_car[1], pos_car[2]);
	psound->SetSourcePosition(gearsound, pos_car[0], pos_car[1], pos_car[2]);
	psound->SetSourcePosition(brakesound, pos_car[0], pos_car[1], pos_car[2]);
	psound->SetSourcePosition(handbrakesound, pos_car[0], pos_car[1], pos_car[2]);

	// update engine sounds
	float rpm = GetEngineRPM();
	float throttle = dynamics.GetEngine().GetThrottle();
	float total_gain = 0.0;

	std::vector<std::pair<size_t, float> > gainlist;
	gainlist.reserve(enginesounds.size());
	for (std::vector<ENGINESOUNDINFO>::iterator i = enginesounds.begin(); i != enginesounds.end(); ++i)
	{
		ENGINESOUNDINFO & info = *i;
		float gain = 1.0;

		if (rpm < info.minrpm)
		{
			gain = 0;
		}
		else if (rpm < info.fullgainrpmstart && info.fullgainrpmstart > info.minrpm)
		{
			gain *= (rpm - info.minrpm) / (info.fullgainrpmstart - info.minrpm);
		}

		if (rpm > info.maxrpm)
		{
			gain = 0;
		}
		else if (rpm > info.fullgainrpmend && info.fullgainrpmend < info.maxrpm)
		{
			gain *= 1.0 - (rpm - info.fullgainrpmend) / (info.maxrpm - info.fullgainrpmend);
		}

		if (info.power == ENGINESOUNDINFO::BOTH)
		{
			gain *= throttle * 0.5 + 0.5;
		}
		else if (info.power == ENGINESOUNDINFO::POWERON)
		{
			gain *= throttle;
		}
		else if (info.power == ENGINESOUNDINFO::POWEROFF)
		{
			gain *= (1.0-throttle);
		}

		total_gain += gain;
		gainlist.push_back(std::make_pair(info.sound_source, gain));

		float pitch = rpm / info.naturalrpm;

		psound->SetSourcePosition(info.sound_source, pos_eng[0], pos_eng[1], pos_eng[2]);
		psound->SetSourcePitch(info.sound_source, pitch);
	}

	// normalize gains
	assert(total_gain >= 0.0);
	for (std::vector<std::pair<size_t, float> >::iterator i = gainlist.begin(); i != gainlist.end(); ++i)
	{
		float gain;
		if (total_gain == 0.0)
		{
			gain = 0.0;
		}
		else if (enginesounds.size() == 1 && enginesounds.back().power == ENGINESOUNDINFO::BOTH)
		{
			gain = i->second;
		}
		else
		{
			gain = i->second / total_gain;
		}
		psound->SetSourceGain(i->first, gain);
	}

	// update tire squeal sounds
	for (int i = 0; i < 4; i++)
	{
		// make sure we don't get overlap
		psound->SetSourceGain(gravelsound[i], 0.0);
		psound->SetSourceGain(grasssound[i], 0.0);
		psound->SetSourceGain(tiresqueal[i], 0.0);

		float squeal = GetTireSquealAmount(WHEEL_POSITION(i));
		float maxgain = 0.3;
		float pitchvariation = 0.4;

		size_t * sound_active;
		const TRACKSURFACE & surface = dynamics.GetWheelContact(WHEEL_POSITION(i)).GetSurface();
		if (surface.type == TRACKSURFACE::ASPHALT)
		{
			sound_active = tiresqueal;
		}
		else if (surface.type == TRACKSURFACE::GRASS)
		{
			sound_active = grasssound;
			maxgain = 0.4; // up the grass sound volume a little
		}
		else if (surface.type == TRACKSURFACE::GRAVEL)
		{
			sound_active = gravelsound;
			maxgain = 0.4;
		}
		else if (surface.type == TRACKSURFACE::CONCRETE)
		{
			sound_active = tiresqueal;
			maxgain = 0.3;
			pitchvariation = 0.25;
		}
		else if (surface.type == TRACKSURFACE::SAND)
		{
			sound_active = grasssound;
			maxgain = 0.25; // quieter for sand
			pitchvariation = 0.25;
		}
		else
		{
			sound_active = tiresqueal;
			maxgain = 0.0;
		}

		btVector3 pos_wheel = dynamics.GetWheelPosition(WHEEL_POSITION(i));
		btVector3 vel_wheel = dynamics.GetWheelVelocity(WHEEL_POSITION(i));
		float pitch = (vel_wheel.length() - 5.0) * 0.1;
		pitch = clamp(pitch, 0.0f, 1.0f);
		pitch = 1.0 - pitch;
		pitch *= pitchvariation;
		pitch = pitch + (1.0 - pitchvariation);
		pitch = clamp(pitch, 0.1f, 4.0f);

		psound->SetSourcePosition(sound_active[i], pos_wheel[0], pos_wheel[1], pos_wheel[2]);
		psound->SetSourcePitch(sound_active[i], pitch);
		psound->SetSourceGain(sound_active[i], squeal * maxgain);
	}

	//update road noise sound
	{
		float gain = dynamics.GetVelocity().length();
		gain *= 0.02;
		gain *= gain;
		if (gain > 1) gain = 1;
		psound->SetSourceGain(roadnoise, gain);
	}
/*
	//update bump noise sound
	{
		for (int i = 0; i < 4; i++)
		{
			suspensionbumpdetection[i].Update(
				dynamics.GetSuspension(WHEEL_POSITION(i)).GetVelocity(),
				dynamics.GetSuspension(WHEEL_POSITION(i)).GetDisplacementFraction(),
				dt);
			if (suspensionbumpdetection[i].JustSettled())
			{
				float bumpsize = suspensionbumpdetection[i].GetTotalBumpSize();

				const float breakevenms = 5.0;
				float gain = bumpsize * GetSpeed() / breakevenms;
				if (gain > 1)
					gain = 1;
				if (gain < 0)
					gain = 0;

				if (gain > 0 && !tirebump[i].Audible())
				{
					tirebump[i].SetGain(gain);
					tirebump[i].Stop();
					tirebump[i].Play();
				}
			}
		}
	}
*/
	//update crash sound
	crashdetection.Update(GetSpeed(), dt);
	float crashdecel = crashdetection.GetMaxDecel();
	if (crashdecel > 0)
	{
		const float mingainat = 200;
		const float maxgainat = 2000;
		float gain = (crashdecel - mingainat) / (maxgainat - mingainat);
		gain = clamp(gain, 0.1f, 1.0f);

		if (!psound->GetSourcePlaying(crashsound))
		{
			psound->ResetSource(crashsound);
			psound->SetSourceGain(crashsound, gain);
		}
	}

	// update gear sound
	if (driver_view && gearsound_check != GetGear())
	{
		float gain = 0.0;
		if (GetEngineRPM() != 0.0)
			gain = GetEngineRPMLimit() / GetEngineRPM();
		gain = clamp(gain, 0.25f, 0.50f);

		if (!psound->GetSourcePlaying(gearsound))
		{
			psound->ResetSource(gearsound);
			psound->SetSourceGain(gearsound, gain);
		}
		gearsound_check = GetGear();
	}
}

void CAR::Update(double dt)
{
	UpdateGraphics();
	UpdateSounds(dt);
}

void CAR::HandleInputs(const std::vector <float> & inputs)
{
	 // ensure that our inputs vector contains exactly one item per input
	assert(inputs.size() == CARINPUT::INVALID);

	// recover from a rollover
	if (inputs[CARINPUT::ROLLOVER_RECOVER])
		dynamics.RolloverRecover();

	// set brakes
	dynamics.SetBrake(inputs[CARINPUT::BRAKE]);
	dynamics.SetHandBrake(inputs[CARINPUT::HANDBRAKE]);

	// do steering
	float steer_value = inputs[CARINPUT::STEER_RIGHT];
	if (std::abs(inputs[CARINPUT::STEER_LEFT]) > std::abs(inputs[CARINPUT::STEER_RIGHT])) //use whichever control is larger
		steer_value = -inputs[CARINPUT::STEER_LEFT];
	dynamics.SetSteering(steer_value);
	last_steer = steer_value;
	QUATERNION<float> steer;
	steer.Rotate(-steer_value * steer_angle_max, 0, 0, 1);
	steer_rotation = steer_orientation * steer;

    // start the engine if requested
	if (inputs[CARINPUT::START_ENGINE])
		dynamics.StartEngine();

	// do shifting
	int gear_change = 0;
	if (inputs[CARINPUT::SHIFT_UP] == 1.0)
		gear_change = 1;
	if (inputs[CARINPUT::SHIFT_DOWN] == 1.0)
		gear_change = -1;
	int cur_gear = dynamics.GetTransmission().GetGear();
	int new_gear = cur_gear + gear_change;

	if (inputs[CARINPUT::REVERSE])
		new_gear = -1;
	if (inputs[CARINPUT::NEUTRAL])
		new_gear = 0;
	if (inputs[CARINPUT::FIRST_GEAR])
		new_gear = 1;
	if (inputs[CARINPUT::SECOND_GEAR])
		new_gear = 2;
	if (inputs[CARINPUT::THIRD_GEAR])
		new_gear = 3;
	if (inputs[CARINPUT::FOURTH_GEAR])
		new_gear = 4;
	if (inputs[CARINPUT::FIFTH_GEAR])
		new_gear = 5;
	if (inputs[CARINPUT::SIXTH_GEAR])
		new_gear = 6;

	applied_brakes = inputs[CARINPUT::BRAKE];

	float throttle = inputs[CARINPUT::THROTTLE];
	float clutch = 1 - inputs[CARINPUT::CLUTCH];
	float nos = inputs[CARINPUT::NOS];

	nos_active = nos > 0;

	dynamics.ShiftGear(new_gear);
	dynamics.SetThrottle(throttle);
	dynamics.SetClutch(clutch);
	dynamics.SetNOS(nos);

	// do driver aid toggles
	if (inputs[CARINPUT::ABS_TOGGLE])
		dynamics.SetABS(!dynamics.GetABSEnabled());

	if (inputs[CARINPUT::TCS_TOGGLE])
		dynamics.SetTCS(!dynamics.GetTCSEnabled());

	// update interior sounds
	if (!psound || !driver_view) return;

/*	// disable brake sound, sounds wierd
	if (inputs[CARINPUT::BRAKE] > 0 && !brakesound_check)
	{
		if (!psound->GetSourcePlaying(brakesound))
		{
			psound->ResetSource(brakesound);
			psound->SetSourceGain(brakesound, 0.5);
		}
		brakesound_check = true;
	}
	if (inputs[CARINPUT::BRAKE] <= 0)
		brakesound_check = false;
*/
	// handbrake sound
	if (inputs[CARINPUT::HANDBRAKE] > 0 && !handbrakesound_check)
	{
		if (!psound->GetSourcePlaying(handbrakesound))
		{
			psound->ResetSource(handbrakesound);
			psound->SetSourceGain(handbrakesound, 0.5);
		}
		handbrakesound_check = true;
	}
	if (inputs[CARINPUT::HANDBRAKE] <= 0)
		handbrakesound_check = false;
}

float CAR::GetFeedback()
{
	return dynamics.GetFeedback() / mz_nominalmax;
}

float CAR::GetTireSquealAmount(WHEEL_POSITION i) const
{
	const TRACKSURFACE & surface = dynamics.GetWheelContact(WHEEL_POSITION(i)).GetSurface();
	if (surface.type == TRACKSURFACE::NONE) return 0;

	btQuaternion wheelspace = dynamics.GetUprightOrientation(WHEEL_POSITION(i));
	btVector3 groundvel = quatRotate(wheelspace.inverse(), dynamics.GetWheelVelocity(WHEEL_POSITION(i)));
	float wheelspeed = dynamics.GetWheel(WHEEL_POSITION(i)).GetAngularVelocity() * dynamics.GetWheel(WHEEL_POSITION(i)).GetRadius();
	groundvel[0] -= wheelspeed;
	groundvel[1] *= 2.0;
	groundvel[2] = 0;
	float squeal = (groundvel.length() - 3.0) * 0.2;

	double sr = dynamics.GetTire(i).getSlip() / dynamics.GetTire(i).getIdealSlip();
	double ar = dynamics.GetTire(i).getSlipAngle() / dynamics.GetTire(i).getIdealSlipAngle();
	double maxratio = std::max(std::abs(sr), std::abs(ar));
	float squealfactor = std::max(0.0, maxratio - 1.0);
	squeal *= squealfactor;
	if (squeal < 0) squeal = 0;
	if (squeal > 1) squeal = 1;

	return squeal;
}

void CAR::SetInteriorView(bool value)
{
	if (driver_view == value) return;

	driver_view = value;

	// disable/enable glass
	SCENENODE & bodynoderef = topnode.GetNode(bodynode);
	keyed_container<DRAWABLE> & normal_blend = bodynoderef.GetDrawlist().normal_blend;
	for (keyed_container<DRAWABLE>::iterator i = normal_blend.begin(); i != normal_blend.end(); ++i)
	{
		i->SetDrawEnable(!driver_view);
	}
}

bool CAR::Serialize(joeserialize::Serializer & s)
{
	_SERIALIZE_(s,dynamics);
	_SERIALIZE_(s,last_steer);
	return true;
}
