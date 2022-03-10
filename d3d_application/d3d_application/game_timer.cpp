#include "game_timer.h"
#include <Windows.h>

game_timer::game_timer() :
	m_secconds_per_count(.0), m_delta_time(-1.0),
	m_base_time(0), m_pause_time(0), m_stop_time(0),
	m_previous_time(0), m_current_time(0), m_stopped(false)
{
	__int64 counts_per_sec{};
	QueryPerformanceFrequency((LARGE_INTEGER*)&counts_per_sec);
	m_secconds_per_count = 1.0 / (double)counts_per_sec;
}

float game_timer::delta_time() const
{
	return static_cast<float>(m_delta_time);
}

float game_timer::total_time() const
{
	if (m_stopped)
	{
		return static_cast<float>((m_stop_time - m_pause_time - m_base_time) * m_secconds_per_count);
	}
	else
	{
		return static_cast<float>((m_current_time - m_pause_time - m_base_time) * m_secconds_per_count);
	}
}

void game_timer::reset()
{
	__int64 curr_time{};
	QueryPerformanceCounter((LARGE_INTEGER*)&curr_time);

	m_base_time = curr_time;
	m_previous_time = curr_time;
	m_stopped = 0;
	m_stopped = false;
}

void game_timer::start()
{
	if (m_stopped)
	{
		__int64 curr_time{};
		QueryPerformanceCounter((LARGE_INTEGER*)&curr_time);

		m_pause_time += curr_time - m_stop_time;

		m_previous_time = curr_time;

		m_stop_time = 0;
		m_stopped = false;
	}
}

void game_timer::stop()
{
	if (!m_stopped)
	{
		__int64 curr_time{};
		QueryPerformanceCounter((LARGE_INTEGER*)&curr_time);

		m_stop_time = curr_time;
		m_stopped = true;
	}
}

void game_timer::tick()
{
	if (m_stopped)
	{
		m_delta_time = .0;
		return;
	}

	__int64 curr_time{};
	QueryPerformanceCounter((LARGE_INTEGER*)&curr_time);
	m_current_time = curr_time;

	m_delta_time = (m_current_time - m_previous_time) * m_secconds_per_count;

	m_previous_time = m_current_time;

	if (m_delta_time < .0)
	{
		m_delta_time = .0;
	}
}
