#pragma once

class game_timer
{
public:
	game_timer();

	float total_time() const;
	float delta_time() const;

	void reset();
	void start();
	void stop();
	void tick();

private:
	double m_secconds_per_count;
	double m_delta_time;

	__int64 m_base_time;
	__int64 m_pause_time;
	__int64 m_stop_time;
	__int64 m_previous_time;
	__int64 m_current_time;

	bool m_stopped;
};