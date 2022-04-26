#pragma once

#include <TimestampUtils.h>
#include <limits>
#include <iostream>

#ifdef USE_PERF_PROBES
#define PERF_PROBES_ENABLED_BOOL true
#else
#define PERF_PROBES_ENABLED_BOOL false
#endif

static const constexpr bool s_perfProbesEnabled = PERF_PROBES_ENABLED_BOOL;


// -------------- Max Tracker code ---------------

class MaxTracker{
public:
	inline MaxTracker(std::string name = "") noexcept : m_name(std::move(name)) {}

#ifdef ENABLE_MAX_TRACKERS
	constexpr static bool s_enabled = true;
	constexpr static size_t s_triggerLevel = MAX_TRACKER_TRIGGER_LEVEL ;
#else
	constexpr static bool s_enabled = false;
	constexpr static size_t s_triggerLevel = std::numeric_limits<size_t>::max();
#endif

	inline auto get()
	{
		return m_globalMax.load(std::memory_order_relaxed);
	}

	inline auto getCount()
	{
		return m_globalTriggeredCount.load(std::memory_order_relaxed);
	}

	constexpr inline  static auto getInitValue(){
		return std::numeric_limits<int64_t>::min();
	}

	inline void update(int64_t & localMax, int64_t value)
	{
		if(value >= static_cast<int64_t>(s_triggerLevel))
			m_globalTriggeredCount.fetch_add(1, std::memory_order_relaxed);

		if(value <= localMax)
			return;

		assert(m_globalMax.load(std::memory_order_relaxed) >= localMax);    // global max currently cannot be reset reliably

		localMax = value;

		int64_t globalMax;
		do{
			globalMax = m_globalMax.load(std::memory_order_relaxed);
			if(globalMax >= localMax)
				return;

			//localMax should be the new max
		}while(!std::atomic_compare_exchange_weak_explicit(
				&m_globalMax,
				&globalMax,
				localMax,
				std::memory_order_relaxed,
				std::memory_order_relaxed));
	}

	static inline void print(std::vector<MaxTracker*> trackers, std::ostream& os = std::cout)
	{
		os << std::endl;
		os << "max(ns)" << '\t' << "count-exceeding-" << MaxTracker::s_triggerLevel << '\t' << "name" << std::endl;
		for(auto * tracker : trackers){
			os << tracker->get() << '\t' << tracker->getCount() << '\t' << tracker->m_name << std::endl;
		}
		os << std::endl;
	}

private:
	std::atomic<int64_t> m_globalMax {std::numeric_limits<int64_t>::min()};
	std::atomic<size_t> m_globalTriggeredCount {0};
	std::string m_name;   //optional
};

struct LocalMaxTracker{
	inline LocalMaxTracker(MaxTracker & globalMaxTracker) noexcept : maxTracker{globalMaxTracker}{}
	inline void update(int64_t value){
		if(!MaxTracker::s_enabled)
			return;

		maxTracker.update(localMax, value);
	}
	MaxTracker & maxTracker;
	int64_t localMax {MaxTracker::getInitValue()};
};

#define MAX_TRACKER(LOCAL_TRACKER,FROM_TIMESTAMP,CURRENT_TIMESTAMP)\
if(MaxTracker::s_enabled)\
{\
	CURRENT_TIMESTAMP = getCurrentTimepoint();\
	LOCAL_TRACKER.update(CURRENT_TIMESTAMP - (FROM_TIMESTAMP));\
}

#define MAX_TRACKER_LOCAL(LOCAL_TRACKER,FROM_TIMESTAMP,CURRENT_TIMESTAMP_NAME)\
int64_t CURRENT_TIMESTAMP_NAME;\
MAX_TRACKER(LOCAL_TRACKER,FROM_TIMESTAMP,CURRENT_TIMESTAMP_NAME)

