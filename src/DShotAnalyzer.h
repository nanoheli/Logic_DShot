#ifndef DSHOT_ANALYZER_H
#define DSHOT_ANALYZER_H

#include <Analyzer.h>
#include "DshotAnalyzerResults.h"
#include "DshotSimulationDataGenerator.h"

class DshotAnalyzerSettings;
class ANALYZER_EXPORT DshotAnalyzer : public Analyzer2
{
public:
	DshotAnalyzer();
	virtual ~DshotAnalyzer();
    virtual void SetupResults();
	virtual void WorkerThread();

	virtual U32 GenerateSimulationData( U64 newest_sample_requested, U32 sample_rate, SimulationChannelDescriptor** simulation_channels );
	virtual U32 GetMinimumSampleRateHz();

	virtual const char* GetAnalyzerName() const;
	virtual bool NeedsRerun();

protected: //vars
	std::unique_ptr< DshotAnalyzerSettings > mSettings;
	std::unique_ptr< DshotAnalyzerResults > mResults;
	AnalyzerChannelData* mSerial;

	DshotSimulationDataGenerator mSimulationDataGenerator;
	bool mSimulationInitilized;

	//Serial analysis vars:
	U32 mSampleRateHz;
	U32 mSamplesPerBit;
    double mSamplesPerBitTLM;
	U32 mStartOfStopBitOffset;
    U32 mEndOfStopBitOffset;
    bool mIsBidir;

	double proportionOfBit(U32 width);
};

extern "C" ANALYZER_EXPORT const char* __cdecl GetAnalyzerName();
extern "C" ANALYZER_EXPORT Analyzer* __cdecl CreateAnalyzer( );
extern "C" ANALYZER_EXPORT void __cdecl DestroyAnalyzer( Analyzer* analyzer );

#endif //DSHOT_ANALYZER_H
