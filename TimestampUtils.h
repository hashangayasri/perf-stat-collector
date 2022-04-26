#pragma once

#include <boost/chrono.hpp>

namespace
{
inline static int64_t getCurrentTimepoint()
{
	return boost::chrono::duration_cast<boost::chrono::nanoseconds>(
			boost::chrono::steady_clock::now().time_since_epoch()).count();
}
}