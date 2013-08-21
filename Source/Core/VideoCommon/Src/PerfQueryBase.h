#ifndef _PERFQUERY_BASE_H_
#define _PERFQUERY_BASE_H_

#include "CommonTypes.h"

enum PerfQueryType
{
	PQ_ZCOMP_INPUT_ZCOMPLOC = 0,
	PQ_ZCOMP_OUTPUT_ZCOMPLOC,
	PQ_ZCOMP_INPUT,
	PQ_ZCOMP_OUTPUT,
	PQ_BLEND_INPUT,
	PQ_EFB_COPY_CLOCKS,
	PQ_NUM_MEMBERS
};

enum PerfQueryGroup
{
	PQG_ZCOMP_ZCOMPLOC,
	PQG_ZCOMP,
	PQG_EFB_COPY_CLOCKS,
	PQG_NUM_MEMBERS,
};

class PerfQueryBase
{
public:
	PerfQueryBase() {}
	virtual ~PerfQueryBase() {}

	// Checks if performance queries are enabled in the gameini configuration.
	bool ShouldEmulate() const;

	// Begin querying the specified value for the following host GPU commands
	virtual void EnableQuery(PerfQueryGroup type) {}

	// Stop querying the specified value for the following host GPU commands
	virtual void DisableQuery(PerfQueryGroup type) {}

	// Reset query counters to zero and drop any pending queries
	virtual void ResetQuery() {}

	// Return the measured value for the specified query type
	// NOTE: Called from CPU thread
	virtual u32 GetQueryResult(PerfQueryType type) { return 0; }

	// Request the value of any pending queries - causes a pipeline flush and thus should be used carefully!
	virtual void FlushResults() {}

	// True if there are no further pending query results
	// NOTE: Called from CPU thread
	virtual bool IsFlushed() const { return true; }
};

extern PerfQueryBase* g_perf_query;

#endif // _PERFQUERY_H_
