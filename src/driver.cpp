/* $Id$ */

/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <http://www.gnu.org/licenses/>.
 */

/** @file driver.cpp Base for all driver handling. */

#include "stdafx.h"
#include "debug.h"
#include "sound/sound_driver.hpp"
#include "music/music_driver.hpp"
#include "video/video_driver.hpp"
#include "string.h"

VideoDriver *_video_driver; ///< The currently active video driver.
char *_ini_videodriver;     ///< The video driver a stored in the configuration file.
int _num_resolutions;       ///< The number of resolutions.
Dimension _resolutions[32]; ///< List of resolutions.
Dimension _cur_resolution;  ///< The current resolution.
bool _rightclick_emulate;   ///< Whether right clicking is emulated.

SoundDriver *_sound_driver; ///< The currently active sound driver.
char *_ini_sounddriver;     ///< The sound driver a stored in the configuration file.

MusicDriver *_music_driver; ///< The currently active music driver.
char *_ini_musicdriver;     ///< The music driver a stored in the configuration file.

char *_ini_blitter;         ///< The blitter as stored in the configuration file.
bool _blitter_autodetected; ///< Was the blitter autodetected or specified by the user?

/**
 * Get a string parameter the list of parameters.
 * @param parm The parameters.
 * @param name The parameter name we're looking for.
 * @return The parameter value.
 */
const char *GetDriverParam(const char * const *parm, const char *name)
{
	size_t len;

	if (parm == NULL) return NULL;

	len = strlen(name);
	for (; *parm != NULL; parm++) {
		const char *p = *parm;

		if (strncmp(p, name, len) == 0) {
			if (p[len] == '=')  return p + len + 1;
			if (p[len] == '\0') return p + len;
		}
	}
	return NULL;
}

/**
 * Get a boolean parameter the list of parameters.
 * @param parm The parameters.
 * @param name The parameter name we're looking for.
 * @return The parameter value.
 */
bool GetDriverParamBool(const char * const *parm, const char *name)
{
	return GetDriverParam(parm, name) != NULL;
}

/**
 * Get an integer parameter the list of parameters.
 * @param parm The parameters.
 * @param name The parameter name we're looking for.
 * @param def  The default value if the parameter doesn't exist.
 * @return The parameter value.
 */
int GetDriverParamInt(const char * const *parm, const char *name, int def)
{
	const char *p = GetDriverParam(parm, name);
	return p != NULL ? atoi(p) : def;
}

/**
 * Find the requested driver and return its class.
 * @param name the driver to select.
 * @param type the type of driver to select
 * @post Sets the driver so GetCurrentDriver() returns it too.
 */
Driver *DriverFactoryBase::SelectDriver(const char *name, Driver::Type type)
{
	if (GetDrivers().size() == 0) return NULL;

	if (StrEmpty(name)) {
		/* Probe for this driver, but do not fall back to dedicated/null! */
		for (int priority = 10; priority > 0; priority--) {
			Drivers::iterator it = GetDrivers().begin();
			for (; it != GetDrivers().end(); ++it) {
				DriverFactoryBase *d = (*it).second;

				/* Check driver type */
				if (d->type != type) continue;
				if (d->priority != priority) continue;

				Driver *newd = d->CreateInstance();
				const char *err = newd->Start(NULL);
				if (err == NULL) {
					DEBUG(driver, 1, "Successfully probed %s driver '%s'", GetDriverTypeName(type), d->name);
					delete *GetActiveDriver(type);
					*GetActiveDriver(type) = newd;
					return newd;
				}

				DEBUG(driver, 1, "Probing %s driver '%s' failed with error: %s", GetDriverTypeName(type), d->name, err);
				delete newd;
			}
		}
		usererror("Couldn't find any suitable %s driver", GetDriverTypeName(type));
	} else {
		char *parm;
		char buffer[256];
		const char *parms[32];

		/* Extract the driver name and put parameter list in parm */
		bstrcpy (buffer, name);
		parm = strchr(buffer, ':');
		parms[0] = NULL;
		if (parm != NULL) {
			uint np = 0;
			/* Tokenize the parm. */
			do {
				*parm++ = '\0';
				if (np < lengthof(parms) - 1) parms[np++] = parm;
				while (*parm != '\0' && *parm != ',') parm++;
			} while (*parm == ',');
			parms[np] = NULL;
		}

		/* Find this driver */
		Drivers::iterator it = GetDrivers().begin();
		for (; it != GetDrivers().end(); ++it) {
			DriverFactoryBase *d = (*it).second;

			/* Check driver type */
			if (d->type != type) continue;

			/* Check driver name */
			if (strcasecmp(buffer, d->name) != 0) continue;

			/* Found our driver, let's try it */
			Driver *newd = d->CreateInstance();

			const char *err = newd->Start(parms);
			if (err != NULL) {
				delete newd;
				usererror("Unable to load driver '%s'. The error was: %s", d->name, err);
			}

			DEBUG(driver, 1, "Successfully loaded %s driver '%s'", GetDriverTypeName(type), d->name);
			delete *GetActiveDriver(type);
			*GetActiveDriver(type) = newd;
			return newd;
		}
		usererror("No such %s driver: %s\n", GetDriverTypeName(type), buffer);
	}
}

/**
 * Build a human readable list of available drivers, grouped by type.
 * @param buf The buffer to write to.
 */
void DriverFactoryBase::GetDriversInfo (stringb *buf)
{
	for (Driver::Type type = Driver::DT_BEGIN; type != Driver::DT_END; type++) {
		buf->append_fmt ("List of %s drivers:\n", GetDriverTypeName(type));

		for (int priority = 10; priority >= 0; priority--) {
			Drivers::iterator it = GetDrivers().begin();
			for (; it != GetDrivers().end(); it++) {
				DriverFactoryBase *d = (*it).second;
				if (d->type != type) continue;
				if (d->priority != priority) continue;
				buf->append_fmt ("%18s: %s\n", d->name, d->GetDescription());
			}
		}

		buf->append ('\n');
	}
}

/**
 * Construct a new DriverFactory.
 * @param type        The type of driver.
 * @param priority    The priority within the driver class.
 * @param name        The name of the driver.
 * @param description A long-ish description of the driver.
 */
DriverFactoryBase::DriverFactoryBase(Driver::Type type, int priority, const char *name, const char *description) :
	type(type), priority(priority), name(name), description(description)
{
	/* Prefix the name with driver type to make it unique */
	char buf[32];
	strecpy(buf, GetDriverTypeName(type), lastof(buf));
	strecpy(buf + 5, name, lastof(buf));

	const char *longname = strdup(buf);

	std::pair<Drivers::iterator, bool> P = GetDrivers().insert(Drivers::value_type(longname, this));
	assert(P.second);
}

/**
 * Frees memory used for this->name
 */
DriverFactoryBase::~DriverFactoryBase()
{
	/* Prefix the name with driver type to make it unique */
	char buf[32];
	bstrfmt (buf, "%s%s", GetDriverTypeName(type),  this->name);

	Drivers::iterator it = GetDrivers().find(buf);
	assert(it != GetDrivers().end());

	const char *longname = (*it).first;

	GetDrivers().erase(it);
	free(longname);

	if (GetDrivers().empty()) delete &GetDrivers();
}
