#pragma once

#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <regex>    // for file name replacement
#include <mutex>

#include <boost/accumulators/accumulators.hpp>
#include <boost/accumulators/statistics.hpp>
#include <boost/accumulators/statistics/tail_quantile.hpp>
#include <boost/accumulators/statistics/p_square_quantile.hpp>

namespace{

static inline void compilerBarrier()
{
	asm volatile ("":::"memory");
}

namespace Stats{
	using namespace boost::accumulators;

	struct StatCollectorAdditionalConfig
	{
		bool printStatName {true};
		bool dumpAllSamplesToOutputStream {false};
		bool dumpAllSamplesToFile
#ifdef DUMP_STAT_SAMPLES
						{true};
#else
						{false};
#endif
		std::string samplesFileName {}; // default name is auto generated, only used if dumpAllSamplesToFile is enabled
	};

	template<typename T>
	class StatCollectorImpl
	{
	public:
		//	using T = long;

		StatCollectorImpl(std::string statName = "", std::string statUnit = "", size_t divisor = 1, StatCollectorAdditionalConfig cfg = {}) :
			m_statName(std::move(statName)), m_statUnit(std::move(statUnit)), m_divisor{divisor}, m_cfg{cfg}
		{
			if(m_cfg.samplesFileName == ""){
				m_cfg.samplesFileName = "stats-" + m_statName + ".tsv"; // use generated file name
				//Replace invalid chars
				m_cfg.samplesFileName = std::regex_replace(m_cfg.samplesFileName, std::regex{("[^-._+()0-9a-zA-Z]")}, "_");
				//Replace repetitions
				m_cfg.samplesFileName = std::regex_replace(m_cfg.samplesFileName, std::regex{("([-._+()])*")}, "$1");
			}
			if(! m_cfg.printStatName)
				m_statName = "";
		}

		virtual inline void record(T value)
		{
			m_acc(std::move(value));
		}

		virtual void printStats(std::ostream& os = std::cout)
		{
			if(count(m_acc) == 0)
				return;
			os << std::setprecision(2) << std::fixed;
			os << getStatLabel("min") << static_cast<double>(min(m_acc))/m_divisor << std::endl;
			os << getStatLabel("max") << static_cast<double>(max(m_acc))/m_divisor << std::endl;
			os << getStatLabel("mean") << static_cast<double>(mean(m_acc))/m_divisor << std::endl;
//			os << getStatLabel("statistical-median") << median(m_acc)/m_divisor << std::endl;
//			os << getStatLabel("statistical-quantile-99.9%") << p_square_quantile(m_acc)/m_divisor << std::endl;
			os << getStatLabelWithoutUnit("count") << count(m_acc) << std::endl;
			m_statsPrinted = true;
		}

		virtual void printStatsOnce(std::ostream& os = std::cout){
			if(!m_statsPrinted)
				printStats(os);
		}

		virtual ~StatCollectorImpl()
		{
			printStatsOnce();
		}

		virtual T getSum()
		{
			return sum(m_acc);
		}

	protected:
		std::string getStatPrefix(){
			return (m_statName == "") ? "" : m_statName + "-";
		}

		std::string getStatPostfix(){
			return (m_statUnit == "") ? "" : " (" + m_statUnit + ")";
		}

		std::string getStatLabelWithoutUnit(std::string statLabel){
			return getStatPrefix() + statLabel + ": ";
		}

		std::string getStatLabel(std::string statLabel){
			return getStatPrefix() + statLabel + getStatPostfix() + ": ";
		}

		std::string m_statName;
		std::string m_statUnit;
		size_t m_divisor;
		StatCollectorAdditionalConfig m_cfg;
	private:
//		accumulator_set<T, features<tag::mean, tag::median, tag::min, tag::max,
//			tag::p_square_quantile, tag::count> > m_acc {quantile_probability = 0.999 };
		accumulator_set<T, features<tag::mean, tag::min, tag::max, tag::count> > m_acc {quantile_probability = 0.999 };
		bool m_statsPrinted {false};
	};

	template<typename T>
	class BasicStatCollector final : public StatCollectorImpl<T> {
		using StatCollectorImpl<T>::StatCollectorImpl;
	};

#ifndef DELAYED_STAT_COLLECTOR_CAPACITY
#define DELAYED_STAT_COLLECTOR_CAPACITY 51000000
#endif

	template<typename T = unsigned long, size_t CAPACITY = DELAYED_STAT_COLLECTOR_CAPACITY>
	class DelayedStatCollector final : public StatCollectorImpl<T> {
		using Base = StatCollectorImpl<T>;

	public:
		constexpr static size_t Capacity = CAPACITY;
		DelayedStatCollector(std::string statName = "", std::string statUnit = "", size_t divisor = 1, StatCollectorAdditionalConfig cfg = {}) :
				Base(std::move(statName), std::move(statUnit), divisor, cfg)
		{
			m_samples.reserve(Capacity);
#ifdef DELAYED_STAT_COLLECTOR_TOUCH_RESERVED
			std::memset(m_samples.data(), 255, sizeof(T) * Capacity);    // check if this is correct when T has alignment requirements
#endif
		}

		inline void record(T value) override
		{
			//If this scenario is frequent, it's better to overwrite the array so the first samples are discarded
			if(Capacity > 0 && __builtin_expect(m_samples.size() < Capacity, 1))
				m_samples.emplace_back(std::move(value));
		}

		void printStats(std::ostream& os = std::cout) override
		{
			if(m_samples.size() == 0)
				return;
			os << std::setprecision(2) << std::fixed;
			if(m_samples.size() >= Capacity)
				std::cerr << "WARNING : Delayed Stat Collector's buffer has overrun. "
				             "Last samples have been discarded" << std::endl;
			{
				auto dumpStatSamples = [&](auto & ostream, std::string prefix = ""){
					for (auto & sample : m_samples)
						ostream << prefix << sample << '\n'; // does not flush
				};

				if(Base::m_cfg.dumpAllSamplesToOutputStream){
					os << "--BEGIN STAT SAMPLE DUMP--" << std::endl;
					auto prefix = "STAT_SAMPLE\t" + ((Base::m_statName.empty()) ? Base::m_statName : (Base::m_statName + '\t'));
					dumpStatSamples(os, prefix);
					os << "--END STAT SAMPLE DUMP--" << std::endl;
				}

				if(Base::m_cfg.dumpAllSamplesToFile){
					//create or replace(truncate) file
					std::ofstream statFile {Base::m_cfg.samplesFileName.c_str() , std::ios::out|std::ios::trunc};
					assert(statFile.good());
					dumpStatSamples(statFile);
					statFile << std::endl;
					statFile.flush();
					statFile.close();
				}
			}

#ifndef DELAYED_STAT_COLLECTOR_NO_MANUAL_SAMPLING
			{
				auto copyOfSamples = m_samples;
				printManualStats(copyOfSamples, os);
			}
#endif

			for (auto & sample : m_samples)
				Base::record(std::move(sample));
			m_samples.clear();
			Base::printStats(os);
		}

		~DelayedStatCollector()
		{
			DelayedStatCollector::printStatsOnce(); // since virtual mechanism is disabled in the parent destructor
		}

		template <typename U = T>
		U getSum(U init)
		{
			if(m_samples.empty())
				return Base::getSum();
			else
				return std::accumulate(m_samples.begin(), m_samples.end(), init);
		}

		T getSum() override
		{
			return getSum(T{});
		}

	private:
		//NOTE: This function sorts samples
		void printManualStats(std::vector<T> & samples, std::ostream& os){
			std::pair<T, T> midMinMaxResult{};
			{
				//					const auto it1Perc = samples.begin()   + samples.size() / 100;
				//					const auto it99Perc = samples.end() - samples.size() / 100;
				//					std::sort(it1Perc, it99Perc);
				//					auto midMinMaxResultIt = (it1Perc == it99Perc) ? it99Perc : std::make_pair(*it1Perc, *(it99Perc - 1));
				auto midMinMaxResultIt = std::minmax_element(
						samples.cbegin()   + samples.size() / 100,
						samples.cend() - 1 - samples.size() / 100
				                                            );
				midMinMaxResult.first  = *midMinMaxResultIt.first;
				midMinMaxResult.second = *midMinMaxResultIt.second;
			}

			std::sort(samples.begin(), samples.end());  //Sort full range
			T quantileResult99  {};
			T quantileResult999 {};
			T quantileResult9999{};
			T medianResult{};
			if (samples.size() > 0){
				quantileResult99   = samples[samples.size() - 1 - samples.size() / 100];
				quantileResult999  = samples[samples.size() - 1 - samples.size() / 1000];
				quantileResult9999 = samples[samples.size() - 1 - samples.size() / 10000];
				medianResult = samples[samples.size() / 2];
			}
			os << Base::getStatLabel("median") << static_cast<double>(medianResult)/Base::m_divisor << std::endl;
			os << Base::getStatLabel("quantile-99%") << static_cast<double>(quantileResult99)/Base::m_divisor << std::endl;
			os << Base::getStatLabel("quantile-99.9%") << static_cast<double>(quantileResult999)/Base::m_divisor << std::endl;
			os << Base::getStatLabel("quantile-99.99%") << static_cast<double>(quantileResult9999)/Base::m_divisor << std::endl;
			os << Base::getStatLabel("min-within-middle-98%-quantile") << static_cast<double>(midMinMaxResult.first)/Base::m_divisor << std::endl;
			os << Base::getStatLabel("max-within-middle-98%-quantile") << static_cast<double>(midMinMaxResult.second)/Base::m_divisor << std::endl;
		}

		std::vector<T> m_samples;
	};

	template<typename T = unsigned long>
#ifdef USE_BASIC_STAT_COLLECTOR
	using StatCollector = BasicStatCollector<T>;
#else
	using StatCollector = DelayedStatCollector<T>;
#endif

}

// ---------------------------------------------------------------------------------------------------------------------

#include "TimestampUtils.h"

class StatCollection
{
	public:
		StatCollection(size_t statCollectorSize) :
		m_statCollectors(statCollectorSize),
		m_lastTimestamps(statCollectorSize, -1)
		{
		}

		void registerStatCollector(size_t statCollectorIndex, const std::string& statCollectorName, const std::string& statUnit)
		{
			std::lock_guard<std::mutex> guard(m_statCollectorRegistration);

			std::cout << "Registering Stat Collector (Index='" << statCollectorIndex << "', Name='" << statCollectorName << "')" << std::endl;

			if (statCollectorIndex >= m_statCollectors.size())
			{
				m_statCollectors.resize(statCollectorIndex + 1);
				m_lastTimestamps.resize(statCollectorIndex + 1);
			}

			m_statCollectors[statCollectorIndex] = std::make_unique<Stats::StatCollector<int64_t>>(statCollectorName, statUnit);
			m_lastTimestamps[statCollectorIndex] = -1;
		}

		size_t registerStatCollector(const std::string& statCollectorName, const std::string& statUnit)
		{
			std::lock_guard<std::mutex> guard(m_statCollectorRegistration);

			auto statCollectorIndex = m_statCollectors.size();
			std::cout << "Registering Stat Collector (Index='" << statCollectorIndex << "', Name='" << statCollectorName << "')" << std::endl;

			auto statCollector = std::make_unique<Stats::StatCollector<int64_t>>(statCollectorName, statUnit);

			m_statCollectors.push_back(std::move(statCollector));
			m_lastTimestamps.push_back(-1);

			return statCollectorIndex;
		}

		void recordStart(size_t statCollectorIndex)
		{
			m_lastTimestamps[statCollectorIndex] = getCurrentTimepoint();
		}

		void recordStart(size_t statCollectorIndex, uint64_t startTimestamp)
		{
			m_lastTimestamps[statCollectorIndex] = startTimestamp;
		}

		void recordEnd(size_t statCollectorIndex)
		{
			auto timespent = getCurrentTimepoint() - m_lastTimestamps[statCollectorIndex];
			m_statCollectors[statCollectorIndex]->record(timespent);
		}

		void recordEnd(size_t statCollectorIndex, uint64_t startTimestamp)
		{
			auto timespent = startTimestamp - m_lastTimestamps[statCollectorIndex];
			m_statCollectors[statCollectorIndex]->record(timespent);
		}

		void recordTimespent(size_t statCollectorIndex, uint64_t timespent)
		{
			m_statCollectors[statCollectorIndex]->record(timespent);
		}

		void printStats(std::ostream& os = std::cout)
		{
			for (auto& statCollector : m_statCollectors)
			{
				if (statCollector) statCollector->printStats(os);
			}
		}

	private:
		std::vector<std::unique_ptr<Stats::StatCollector<int64_t>>> m_statCollectors;
		std::vector<int64_t> m_lastTimestamps;

		std::mutex m_statCollectorRegistration;

	public:
		static StatCollection m_statCollection;
};

StatCollection StatCollection::m_statCollection(100);

#define CollectStats

#ifdef CollectStats
	#define RegisterStatCollector(statCollectorIndex, statCollectorName) StatCollection::m_statCollection.registerStatCollector(statCollectorIndex, statCollectorName, "ns");
	#define CreateStatCollector(statCollectorName)						 StatCollection::m_statCollection.registerStatCollector(statCollectorName, "ns");
	#define StatCollectorRecordStart(statCollectorIndex)				 StatCollection::m_statCollection.recordStart(statCollectorIndex);
	#define StatCollectorRecordEnd(statCollectorIndex)					 StatCollection::m_statCollection.recordEnd(statCollectorIndex);
	#define StatCollectorPrintStats(stream)								 StatCollection::m_statCollection.printStats(stream);
#else
	#define RegisterStatCollector(statCollectorIndex, statCollectorName)
	#define CreateStatCollector(statCollectorName)					     0
	#define StatCollectorRecordStart(statCollectorIndex)
	#define StatCollectorRecordEnd(statCollectorIndex)
	#define StatCollectorPrintStats(stream)
#endif
}