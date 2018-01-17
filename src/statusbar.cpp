/***************************************************************************
 *   Copyright (C) 2008-2017 by Andrzej Rybczak                            *
 *   electricityispower@gmail.com                                          *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include "global.h"
#include "settings.h"
#include "status.h"
#include "statusbar.h"
#include "bindings.h"
#include "screens/playlist.h"
#include "utility/wide_string.h"

using Global::wFooter;

namespace {

bool progressbar_block_update = false;

boost::posix_time::ptime statusbar_lock_time;
boost::posix_time::seconds statusbar_lock_delay(-1);

bool statusbar_block_update = false;
bool statusbar_allow_unlock = true;

}

Progressbar::ScopedLock::ScopedLock() noexcept
{
	progressbar_block_update = true;
}

Progressbar::ScopedLock::~ScopedLock() noexcept
{
	progressbar_block_update = false;
}

bool Progressbar::isUnlocked()
{
	return !progressbar_block_update;
}

void Progressbar::draw(unsigned int elapsed, unsigned int time)
{
	unsigned pb_width = wFooter->getWidth();
	unsigned howlong = time ? pb_width*elapsed/time : 0;
	*wFooter << Config.progressbar_color;
	if (Config.progressbar[2] != '\0')
	{
		wFooter->goToXY(0, 0);
		for (unsigned i = 0; i < pb_width; ++i)
			*wFooter << Config.progressbar[2];
		wFooter->goToXY(0, 0);
	}
	else
		mvwhline(wFooter->raw(), 0, 0, 0, pb_width);
	*wFooter << NC::FormattedColor::End<>(Config.progressbar_color);
	if (time)
	{
		*wFooter << Config.progressbar_elapsed_color;
		pb_width = std::min(size_t(howlong), wFooter->getWidth());
		for (unsigned i = 0; i < pb_width; ++i)
			*wFooter << Config.progressbar[0];
		if (howlong < wFooter->getWidth())
			*wFooter << Config.progressbar[1];
		*wFooter << NC::FormattedColor::End<>(Config.progressbar_elapsed_color);
	}
}

Statusbar::ScopedLock::ScopedLock() noexcept
{
	// lock
	if (Config.statusbar_visibility)
		statusbar_block_update = true;
	else
		progressbar_block_update = true;
	statusbar_allow_unlock = false;
}

Statusbar::ScopedLock::~ScopedLock() noexcept
{
	// unlock
	statusbar_allow_unlock = true;
	if (statusbar_lock_delay.is_negative())
	{
		if (Config.statusbar_visibility)
			statusbar_block_update = false;
		else
			progressbar_block_update = false;
	}
	if (Status::State::player() == MPD::psStop)
	{
		switch (Config.design)
		{
			case Design::Classic:
				put(); // clear statusbar
				break;
			case Design::Alternative:
				Progressbar::draw(Status::State::elapsedTime(), Status::State::totalTime());
				break;
		}
		wFooter->refresh();
	}
}

bool Statusbar::isUnlocked()
{
	return !statusbar_block_update;
}


NC::Window &Statusbar::put()
{
	*wFooter << NC::XY(0, Config.statusbar_visibility ? 1 : 0) << NC::TermManip::ClearToEOL;
	return *wFooter;
}

void Statusbar::print(int delay, const std::string &message)
{
	if (statusbar_allow_unlock)
	{
		statusbar_lock_time = Global::Timer;
		statusbar_lock_delay = boost::posix_time::seconds(delay);
		if (Config.statusbar_visibility)
			statusbar_block_update = true;
		else
			progressbar_block_update = true;
		wFooter->goToXY(0, Config.statusbar_visibility);
		*wFooter << message << NC::TermManip::ClearToEOL;
		wFooter->refresh();
	}
}

void Statusbar::Helpers::mpd()
{
	Status::update(Mpd.noidle());
}

bool Statusbar::Helpers::mainHook(const char *)
{
	Status::trace();
	return true;
}

char Statusbar::Helpers::promptReturnOneOf(const std::vector<char> &values)
{
	if (values.empty())
		throw std::logic_error("empty vector of acceptable input");
	NC::Key::Type result;
	do
	{
		wFooter->refresh();
		result = wFooter->readKey();
		if (result == NC::Key::Ctrl_C || result == NC::Key::Ctrl_G)
			throw NC::PromptAborted();
	}
	while (std::find(values.begin(), values.end(), result) == values.end());
	return result;
}

bool Statusbar::Helpers::ImmediatelyReturnOneOf::operator()(const char *s) const
{
	Status::trace();
	return !isOneOf(s);
}

bool Statusbar::Helpers::ApplyFilterImmediately::operator()(const char *s)
{
	using Global::myScreen;
	Status::trace();
	try {
		if (m_w->allowsFiltering() && m_w->currentFilter() != s)
		{
			m_w->applyFilter(s);
			if (myScreen == myPlaylist)
				myPlaylist->enableHighlighting();
			myScreen->refreshWindow();
		}
	} catch (boost::bad_expression &) { }
	return true;
}

bool Statusbar::Helpers::FindImmediately::operator()(const char *s)
{
	using Global::myScreen;
	Status::trace();
	try {
		if (m_w->allowsSearching() && m_w->searchConstraint() != s)
		{
			m_w->setSearchConstraint(s);
			m_w->search(m_direction, Config.wrapped_search, false);
			if (myScreen == myPlaylist)
				myPlaylist->enableHighlighting();
			myScreen->refreshWindow();
		}
	} catch (boost::bad_expression &) { }
	return true;
}

bool Statusbar::Helpers::TryExecuteImmediateCommand::operator()(const char *s)
{
	bool continue_ = true;
	if (m_s != s)
	{
		m_s = s;
		auto cmd = Bindings.findCommand(m_s);
		if (cmd && cmd->immediate())
			continue_ = false;
	}
	Status::trace();
	return continue_;
}
