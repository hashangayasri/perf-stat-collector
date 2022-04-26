#pragma once

namespace util
{
	void inline doWork(const boost::chrono::nanoseconds& busyTime)
	{
		auto endTime = boost::chrono::steady_clock::now() + busyTime;
		while (boost::chrono::steady_clock::now() < endTime);
	}

	void inline doWorkUntil(const boost::chrono::time_point<boost::chrono::steady_clock>& endTimePoint)
	{
		while (boost::chrono::steady_clock::now() < endTimePoint);
	}
}
