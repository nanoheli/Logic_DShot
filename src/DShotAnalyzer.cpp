#include "DshotAnalyzer.h"
#include "DshotAnalyzerSettings.h"
#include <AnalyzerChannelData.h>
#include <AnalyzerHelpers.h>

#include <stdint.h>
#include <cstdio>

DshotAnalyzer::DshotAnalyzer()
:	Analyzer2(),  
	mSettings( new DshotAnalyzerSettings() ),
	mSimulationInitilized( false )
{
	SetAnalyzerSettings( mSettings.get() );
    UseFrameV2();
}

DshotAnalyzer::~DshotAnalyzer()
{
	KillThread();
}

void DshotAnalyzer::SetupResults()
{
    mResults.reset( new DshotAnalyzerResults( this, mSettings.get() ) );
    SetAnalyzerResults( mResults.get() );
    mResults->AddChannelBubblesWillAppearOn( mSettings->mInputChannel );
}

double DshotAnalyzer::proportionOfBit(U32 width)
{
	return static_cast<double>(width) / mSamplesPerBit;
}

void DshotAnalyzer::WorkerThread()
{
	mSampleRateHz = GetSampleRate();

	mSerial = GetAnalyzerChannelData( mSettings->mInputChannel );
	mSamplesPerBit = mSampleRateHz / (mSettings->mDshotRate * 1000);
    mSamplesPerBitTLM = mSampleRateHz / ( (5/4.)*mSettings->mDshotRate * 1000 );
    double telemFrameLengthMax_s = (mSamplesPerBitTLM * 20.0) / (double)mSampleRateHz * 1.2; // allow 20% timing error

	if( mSettings->mDShotIsBidir )
    {//inverted signal
		if( mSerial->GetBitState() == BIT_LOW )
			mSerial->AdvanceToNextEdge();
    } else {
        if( mSerial->GetBitState() == BIT_HIGH )
            mSerial->AdvanceToNextEdge();
	}	

	uint32_t width = 0;
    uint64_t lastEdge = 0;
	bool isTelem = false;

	for (;;) {
		uint16_t data = 0;
        uint16_t dataTLM = 0;
		uint64_t starting_sample = 0;
		int i;
        FrameV2 framev2;
        char* framev2Type = nullptr;
        isTelem = false;


		for( i = sizeof( data ) * 8 - 1; i >= 0; i-- )
        {
            if( mSerial->GetBitState() == BIT_HIGH && mSettings->mDShotIsBidir )
			{
				mSerial->AdvanceToNextEdge();
			}
            if( mSerial->GetBitState() == BIT_LOW && !( mSettings->mDShotIsBidir ) )
            {
                mSerial->AdvanceToNextEdge();
            }
            //mSerial->AdvanceToNextEdge(); // rising edge of first bit
            uint64_t rising_sample = mSerial->GetSampleNumber();

            uint64_t distFromLastSample = rising_sample - lastEdge;
            uint64_t currentSample = 0;
            uint64_t lastSample = 0;
            double distFromStart_s = 0;
            bool states[ 40 ] = { 0 };
            states[0] = 0;
            uint64_t sampleChange[ 40 ] = { 0 };
            double timeLastEdge = distFromLastSample / (double)mSampleRateHz;
            if( timeLastEdge > 15e-6 && timeLastEdge < 45e-6 ) //we need to handle a telemetry frame
            { 
				isTelem = true;

                uint64_t telemStart = rising_sample;
				//traverse through all samples and find end of frame
                uint64_t stateCounter = 1;
                do
                {
                    lastSample = currentSample;
                    mSerial->AdvanceToNextEdge();
                    states[ stateCounter ] = mSerial->GetBitState();
					sampleChange[ stateCounter ] = mSerial->GetSampleNumber();
					stateCounter++;

                    currentSample = mSerial->GetSampleNumber();
                    distFromStart_s = ( currentSample - telemStart ) / (double)mSampleRateHz;
                } while( distFromStart_s < telemFrameLengthMax_s );
                uint64_t telemEnd = lastSample;
				double samplesPerBitTLMCalc = (telemEnd - telemStart) / 20.0;

                AnalyzerResults::MarkerType marker;
                currentSample = telemStart + samplesPerBitTLMCalc / 2;
                stateCounter = 0;
                //mSerial->AdvanceToAbsPosition( currentSample ); // skip to the middle of the first bit and samle it
				for (int j = 20-1; j >= 0; j--)
				{
					//uint16_t bit = mSerial->GetBitState();
                    if( currentSample > sampleChange[ stateCounter+1 ])
					{
                        stateCounter++;
					}
                    uint16_t bit = states[ stateCounter ];
                    dataTLM |= bit << j;
                    if(bit)
						marker = AnalyzerResults::One;
					else
						marker = AnalyzerResults::Zero;
                    mResults->AddMarker( currentSample, marker, mSettings->mInputChannel );
                    currentSample = telemStart + samplesPerBitTLMCalc / 2 + ( 20 - j ) * samplesPerBitTLMCalc;
                    //mSerial->AdvanceToAbsPosition( currentSample );
				}
                Frame frame;
                frame.mData1 = dataTLM;
                frame.mFlags = 1 << 6; // telem flag
                frame.mStartingSampleInclusive = telemStart;
                frame.mEndingSampleInclusive = currentSample;
                mResults->AddFrame( frame );

                framev2Type = "tlm";
                //framev2.AddBoolean( "crc ok", crcok );
                framev2.AddInteger( "data", dataTLM );
                mResults->AddFrameV2( framev2, framev2Type, telemStart, currentSample );
                mResults->CommitPacketAndStartNewPacket();
                mResults->CommitResults();
                lastEdge = currentSample;
                break;
     
            }

			if (!starting_sample)
				starting_sample = rising_sample;
			mSerial->AdvanceToNextEdge();
			uint64_t falling_sample = mSerial->GetSampleNumber();

			width = falling_sample - rising_sample;
			bool set = proportionOfBit(width) > 0.5;
			// check if low pulse is too long / next bit is too far away
			bool error = i > 0 && proportionOfBit(mSerial->GetSampleOfNextEdge() - falling_sample) > 1.5;
			// check if high pulse is too short
			error |= proportionOfBit(width) < 0.2;

			if (set)
				data |= 1 << i;

			AnalyzerResults::MarkerType marker;
			if (error) {
				marker = AnalyzerResults::ErrorX;
			} else if (i == 4) { // telem request
                if( set )
                {
                    marker = AnalyzerResults::Start;
                    framev2.AddBoolean( "request telemetry", true );
                }
                else
                {
                    marker = AnalyzerResults::Stop;
                    framev2.AddBoolean( "request telemetry", false );
                }
					
			} else { // channel bits or crc
				if (set)
					marker = AnalyzerResults::One;
				else
					marker = AnalyzerResults::Zero;
			}
			mResults->AddMarker(rising_sample + width / 2, marker, mSettings->mInputChannel);

			if (error)
				break;
		}

		if (i >= 0) { // message ended early / bit was errored
			mResults->CommitResults();
			continue;
		}

		uint16_t chan = data & 0xffe0;
		uint8_t crc =	((chan >> 4 ) & 0xf) ^
						((chan >> 8 ) & 0xf) ^
						((chan >> 12) & 0xf);
        bool crcok = 0;
        if( mSettings->mDShotIsBidir )
        {
            crcok = ( ~data & 0xf ) == crc;
		} else {
			crcok = ( data & 0xf ) == crc;
		}
		chan >>= 5;

		//mSerial->Advance(mSamplesPerBit - width); // end of low pulse

		if (!crcok)
			mResults->AddMarker(mSerial->GetSampleNumber(), AnalyzerResults::ErrorX, mSettings->mInputChannel);
		
		//we have a byte to save. 
		Frame frame;
		frame.mData1 = chan;
        frame.mData2 = data;
		frame.mFlags = (crcok ? 0 : DISPLAY_AS_ERROR_FLAG) | ((data & 0x10) > 0); // error flag | telem request
		frame.mStartingSampleInclusive = starting_sample;
		frame.mEndingSampleInclusive = mSerial->GetSampleNumber();

		framev2Type = "cmd";
        framev2.AddBoolean( "crc ok", crcok );
        framev2.AddInteger( "data" , data );
        framev2.AddInteger( "payload", data >> 5);
        framev2.AddInteger( "crc",  crc );

		mResults->AddFrame(frame);
        mResults->AddFrameV2( framev2, framev2Type, starting_sample, crcok ? mSerial->GetSampleNumber()  : starting_sample );
        mResults->CommitPacketAndStartNewPacket();
		mResults->CommitResults();

        lastEdge = mSerial->GetSampleNumber();

		CheckIfThreadShouldExit();
	}
}

bool DshotAnalyzer::NeedsRerun()
{
	return false;
}

U32 DshotAnalyzer::GenerateSimulationData( U64 minimum_sample_index, U32 device_sample_rate, SimulationChannelDescriptor** simulation_channels )
{
	if( mSimulationInitilized == false )
	{
		mSimulationDataGenerator.Initialize( GetSimulationSampleRate(), mSettings.get() );
		mSimulationInitilized = true;
	}

	return mSimulationDataGenerator.GenerateSimulationData( minimum_sample_index, device_sample_rate, simulation_channels );
}

U32 DshotAnalyzer::GetMinimumSampleRateHz()
{
	return mSettings->mDshotRate * 4000;
}

const char* DshotAnalyzer::GetAnalyzerName() const
{
	return "Dshot";
}

const char* GetAnalyzerName()
{
	return "Dshot";
}

Analyzer* CreateAnalyzer()
{
	return new DshotAnalyzer();
}

void DestroyAnalyzer( Analyzer* analyzer )
{
	delete analyzer;
}